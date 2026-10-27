#include "acoustic-medium.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace dahello {

const char *
frameKindName (int k)
{
  static const char *n[] = {"data", "ack", "hello", "beacon"};
  return (k >= 0 && k < kFrameKinds) ? n[k] : "?";
}

MacMode
parseMacMode (const std::string &s)
{
  if (s == "ideal") return MacMode::Ideal;
  if (s == "aloha") return MacMode::Aloha;
  if (s == "cs") return MacMode::Cs;
  if (s == "slotted") return MacMode::Slotted;
  throw std::invalid_argument ("mac must be legacy|ideal|aloha|cs|slotted, got " + s);
}

AcousticMedium::AcousticMedium (const AcousticPhy &phy, MediumConfig cfg, uint32_t n, Sched sched,
                                std::function<double ()> now)
    : phy_ (phy), cfg_ (cfg), nd_ (n), sched_ (std::move (sched)), now_ (std::move (now)),
      rPer_ (cfg.seed * 7919 + 6), rMac_ (cfg.seed * 7919 + 4)
{
  lockLin_ = std::pow (10, phy_.lockDb () / 10);
  csLin_ = std::pow (10, phy_.csDb () / 10);
  floorLin_ = std::pow (10, phy_.floorDb () / 10);
}

double
AcousticMedium::dist (uint32_t a, uint32_t b) const
{
  double ax, ay, az, bx, by, bz;
  pos (a, ax, ay, az);
  pos (b, bx, by, bz);
  return std::sqrt ((ax - bx) * (ax - bx) + (ay - by) * (ay - by) + (az - bz) * (az - bz));
}

void
AcousticMedium::at (double t, std::function<void ()> f)
{
  sched_ (std::max (0.0, t - now_ ()), std::move (f));
}

double
AcousticMedium::tTx (uint32_t n) const
{
  const Node &N = nd_[n];
  const double rem = N.alive ? std::max (0.0, N.txUntil - now_ ()) : 0.0;
  return N.ttx - rem;
}

double
AcousticMedium::tRx (uint32_t n) const
{
  const Node &N = nd_[n];
  return N.trx + ((N.alive && N.locked) ? std::max (0.0, now_ () - N.lockT0) : 0.0);
}

double
AcousticMedium::tAlive (uint32_t n) const
{
  return nd_[n].alive ? now_ () : nd_[n].tDead;
}

bool
AcousticMedium::sensedBusy (uint32_t n) const
{
  const Node &N = nd_[n];
  const double t = now_ ();
  if (N.txUntil > t + kTimeEps || N.locked)
    {
      return true;
    }
  if (cfg_.csOracle)
    {
      for (uint32_t k = 0; k < nd_.size (); ++k)
        {
          if (k != n && nd_[k].alive && nd_[k].txUntil > t + kTimeEps && dist (n, k) < cfg_.rangeM)
            {
              return true;
            }
        }
      return false;
    }
  for (const Arr &a : N.arr)
    {
      if (a.t0 <= t && t < a.t1 && a.pw >= csLin_)
        {
          return true;
        }
    }
  return false;
}

void
AcousticMedium::prune (Node &R)
{
  const double t = now_ () - 1.0;
  size_t w = 0;
  for (size_t r = 0; r < R.arr.size (); ++r)
    {
      if (R.arr[r].t1 >= t)
        {
          R.arr[w++] = R.arr[r];
        }
    }
  R.arr.resize (w);
}

void
AcousticMedium::enqueue (uint32_t n, MediumFrame f, bool front)
{
  Node &N = nd_[n];
  if (!N.alive)
    {
      return;
    }
  if (static_cast<int> (N.q.size ()) >= cfg_.queueCap)
    {
      ++st_.qDrop;
      if (onMacDrop) onMacDrop (n, f, 0);
      return;
    }
  if (front) N.q.push_front (std::move (f)); else N.q.push_back (std::move (f));
  if (!N.macBusy)
    {
      N.macBusy = true;
      N.bo = 0;
      at (now_ (), [this, n] { attempt (n); });
    }
}

void
AcousticMedium::attempt (uint32_t n)
{
  Node &N = nd_[n];
  if (!N.alive)
    {
      N.macBusy = false;
      return;
    }
  while (!N.q.empty () && wanted && !wanted (n, N.q.front ()))
    {
      ++st_.unwanted;
      N.q.pop_front ();
    }
  if (N.q.empty ())
    {
      N.macBusy = false;
      return;
    }
  const double t = now_ ();
  if (N.txUntil > t + kTimeEps)
    {
      at (N.txUntil, [this, n] { attempt (n); });
      return;
    }
  if (cfg_.mac == MacMode::Cs)
    {
      ++st_.csAttempts;
      if (sensedBusy (n))
        {
          ++st_.csBusy;
          ++N.bo;
          if (N.bo > cfg_.boMaxAttempts)
            {
              ++st_.macDrop;
              MediumFrame f = N.q.front ();
              N.q.pop_front ();
              N.bo = 0;
              if (onMacDrop) onMacDrop (n, f, 1);
              at (t, [this, n] { attempt (n); });
              return;
            }
          const double base = cfg_.boBase > 0 ? cfg_.boBase
                                               : phy_.txTime (cfg_.dataBits) + cfg_.rangeM / phy_.config ().c;
          std::uniform_real_distribution<double> u (0.0, base);
          const double w = u (rMac_) * std::ldexp (1.0, std::min (N.bo - 1, 4));
          at (t + w, [this, n] { attempt (n); });
          return;
        }
    }
  else if (cfg_.mac == MacMode::Slotted)
    {
      const double slot = phy_.txTime (cfg_.slotBits) + cfg_.rangeM / phy_.config ().c;
      const double k = std::ceil (t / slot - 1e-9) * slot;
      if (k > t + 1e-9)
        {
          at (k, [this, n] { attempt (n); });
          return;
        }
    }
  MediumFrame f = N.q.front ();
  N.q.pop_front ();
  N.bo = 0;
  transmit (n, std::move (f));
}

void
AcousticMedium::endLock (Node &R, double tEnd)
{
  const double d = std::max (0.0, tEnd - R.lockT0);
  R.trx += d;
  R.trxKind[R.lockFrame.kind] += d;
  R.locked = false;
  R.lockFrame = MediumFrame ();
}

void
AcousticMedium::transmit (uint32_t s, MediumFrame f)
{
  Node &S = nd_[s];
  const double t0 = now_ ();
  const double T = phy_.txTime (f.bits);
  const double t1 = t0 + T;
  f.src = s;
  f.txId = ++txSeq_;
  if (onTxStart) onTxStart (s, f);
  if (S.locked && cfg_.mac != MacMode::Ideal)
    {
      ++st_.hdAbortByKind[S.lockFrame.kind];
      endLock (S, t0);
    }
  S.txUntil = t1;
  S.ttx += T;
  S.ttxKind[f.kind] += T;
  S.curTxKind = f.kind;
  S.lastTx = t0;
  ++st_.txByKind[f.kind];
  st_.bitsByKind[f.kind] += static_cast<uint64_t> (f.bits);
  st_.busyTime += T;
  const double c = phy_.config ().c;
  for (uint32_t r = 0; r < nd_.size (); ++r)
    {
      if (r == s || !nd_[r].alive)
        {
          continue;
        }
      const double snr = linkSnrDb (s, r);
      if (!std::isfinite (snr))
        {
          continue;
        }
      const double pw = std::pow (10, snr / 10);
      if (pw < floorLin_)
        {
          continue;
        }
      const double dl = dist (s, r) / c;
      Node &R = nd_[r];
      if (R.arr.size () > 256) prune (R);
      R.arr.push_back ({f.txId, t0 + dl, t1 + dl, pw, f.kind});
      if (pw >= lockLin_)
        {
          const uint64_t id = f.txId;
          at (t0 + dl, [this, r, id, f, t0, t1, dl, pw] { rxStart (r, id, f, t0 + dl, t1 + dl, pw); });
        }
    }
  at (t1, [this, s, f] { txEnd (s, f); });
}

void
AcousticMedium::txEnd (uint32_t s, MediumFrame f)
{
  Node &N = nd_[s];
  if (onTxEnd && N.alive) onTxEnd (s, f);
  if (N.alive && !N.q.empty ()) attempt (s); else N.macBusy = false;
}

void
AcousticMedium::rxStart (uint32_t r, uint64_t id, const MediumFrame &f, double a0, double a1, double pw)
{
  Node &R = nd_[r];
  if (!R.alive)
    {
      return;
    }
  ++st_.rxStarts;
  if (cfg_.mac == MacMode::Ideal)
    {
      R.trx += a1 - a0;
      R.trxKind[f.kind] += a1 - a0;
      at (a1, [this, r, f, pw] {
        if (!nd_[r].alive) return;
        const double ps = phy_.psucc (pw, f.bits);
        std::uniform_real_distribution<double> u (0.0, 1.0);
        if (ps >= 1.0 || (ps > 0 && u (rPer_) < ps))
          {
            ++st_.okByKind[f.kind];
            if (onDecode) onDecode (r, f, 10 * std::log10 (pw));
          }
        else
          {
            ++st_.noiseByKind[f.kind];
          }
      });
      return;
    }
  const double t = now_ ();
  if (R.txUntil > t + kTimeEps)
    {
      ++st_.hdLossByKind[f.kind];
      return;
    }
  if (R.locked && R.lockT1 <= t + kTimeEps)
    {
      rxEnd (r, R.lockTx); // a frame ending at this instant ends before a new one starts
    }
  if (R.locked)
    {
      ++st_.busyMissByKind[f.kind];
      return;
    }
  R.locked = true;
  R.lockTx = id;
  R.lockT0 = a0;
  R.lockT1 = a1;
  R.lockPw = pw;
  R.lockFrame = f;
  at (a1, [this, r, id] { rxEnd (r, id); });
}

void
AcousticMedium::maxInterf (const Node &R, uint64_t id, double a0, double a1, double &imax, bool &ctrl) const
{
  std::vector<std::pair<double, double>> ev;
  ctrl = false;
  for (const Arr &a : R.arr)
    {
      if (a.tx != id && a.t1 > a0 && a.t0 < a1)
        {
          ev.push_back ({std::max (a.t0, a0), a.pw});
          ev.push_back ({std::min (a.t1, a1), -a.pw});
          if (a.kind == kFrameHello || a.kind == kFrameBeacon) ctrl = true;
        }
    }
  imax = 0.0;
  if (ev.empty ()) return;
  std::sort (ev.begin (), ev.end (), [] (const auto &x, const auto &y) {
    return x.first < y.first || (x.first == y.first && x.second < y.second);
  });
  double cur = 0;
  for (const auto &e : ev)
    {
      cur += e.second;
      imax = std::max (imax, cur);
    }
}

void
AcousticMedium::rxEnd (uint32_t r, uint64_t id)
{
  Node &R = nd_[r];
  if (!(R.alive && R.locked && R.lockTx == id))
    {
      return;
    }
  const MediumFrame f = R.lockFrame;
  const double a0 = R.lockT0, a1 = R.lockT1, pw = R.lockPw;
  endLock (R, a1);
  double I = 0;
  bool ctrl = false;
  maxInterf (R, id, a0, a1, I, ctrl);
  const double ps = phy_.psucc (pw / (1 + I), f.bits);
  std::uniform_real_distribution<double> u (0.0, 1.0);
  const bool ok = (ps >= 1.0) || (ps > 0 && u (rPer_) < ps);
  if (!ok)
    {
      const double ps0 = phy_.psucc (pw, f.bits);
      if (I > 0 && ps0 > ps + 1e-9) ++st_.collByKind[f.kind][ctrl ? 1 : 0];
      else ++st_.noiseByKind[f.kind];
      return;
    }
  ++st_.okByKind[f.kind];
  if (onDecode) onDecode (r, f, 10 * std::log10 (pw));
}

void
AcousticMedium::kill (uint32_t n)
{
  Node &N = nd_[n];
  if (!N.alive) return;
  const double t = now_ ();
  if (N.txUntil > t + kTimeEps)
    {
      // the launched signal keeps propagating; the modem stops drawing tx power now
      N.ttx -= N.txUntil - t;
      N.ttxKind[N.curTxKind] -= N.txUntil - t;
      N.txUntil = t;
    }
  if (N.locked) endLock (N, t);
  N.alive = false;
  N.tDead = t;
  N.q.clear ();
}

} // namespace dahello
