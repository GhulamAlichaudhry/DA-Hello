// Port A (2026-10-12): shared acoustic medium wiring for scenario-i-lqarp.
// Included once, after the Pipeline definition. Every path here runs only
// when --mac != legacy or --chan=formula; the defaults leave the legacy engine
// untouched (Gate 1).
//
// Stage M1 (this file): data, ACK and MARV frames go through the medium
// (finite tx time, propagation delay, SINR/PER, half-duplex, CS/ALOHA, ARQ);
// control frames (HELLO / bootstrap / host-state / energy beacon) occupy the
// channel and draw energy, but their hold writes are still applied by the
// legacy scheduler (oracle reception). Heard-only discovery is stage M2.

namespace {

// Formula channel behind the ChannelGate seam, so every host's N_SNR / c_lin /
// r_range uses the same physics as the medium. Threshold = snr_req.
class FormulaGate : public dahello::ChannelGate
{
public:
  explicit FormulaGate (const dahello::AcousticPhy &phy) : phy_ (phy)
  {
    setSnrThresholdDb (phy.snrReqDb ());
  }
  dahello::ChannelSample queryLink (double txX, double txY, double txZ, double rxX, double rxY,
                                  double rxZ) const override
  {
    return at (distance3D (txX, txY, txZ, rxX, rxY, rxZ));
  }
  dahello::ChannelSample queryRangeDepth (double rangeM, double /*receiverZ*/) const override
  {
    return at (rangeM);
  }
  double rangeAtSnrThreshold () const override
  {
    return phy_.formulaRangeForSnr (phy_.snrReqDb ());
  }
  bool depthIndependent () const override { return true; } // queryRangeDepth ignores receiverZ

private:
  dahello::ChannelSample at (double d) const
  {
    dahello::ChannelSample s;
    // OPT-3: one TL evaluation; snr = sl + di - tl - nl in the same order as formulaSnrDb (same bits)
    s.tl_db = phy_.tlFormulaDb (d);
    const dahello::AcousticPhyConfig &c = phy_.config ();
    s.snr_db = c.sl + c.di - s.tl_db - c.nl;
    s.valid = std::isfinite (s.snr_db);
    s.nearPressureRelease = false;
    return s;
  }
  const dahello::AcousticPhy &phy_;
};

} // namespace

void
Pipeline::MakePhy ()
{
  if (aphy)
    {
      return;
    }
  dahello::AcousticPhyConfig pc;
  pc.chan = chanMode;
  pc.bKhz = phyBkHz;
  pc.rbBps = phyRbBps;
  pc.tPre = phyTpreS;
  pc.ropM = phyRopM;
  pc.shipping = phyShipping;
  pc.windMps = phyWindMps;
  pc.sl = phySl;
  std::string per = phyPer;
  if (per == "auto")
    {
      // formula: frozen BFSK PER; bellhop: the legacy reception gate as a hard
      // SINR threshold (8.68 dB), so the Bellhop attribution step isolates the MAC.
      per = (chanMode == "formula") ? "bfsk" : "thr";
    }
  pc.per = per;
  pc.thrDb = std::isfinite (phyThr) ? phyThr : dahello::ChannelGate::kDefaultSnrThresholdDb;
  aphy = std::make_unique<dahello::AcousticPhy> (pc);
}

std::unique_ptr<dahello::ChannelGate>
Pipeline::MakeFormulaGate ()
{
  MakePhy ();
  return std::make_unique<FormulaGate> (*aphy);
}

void
Pipeline::MedPos (uint32_t i, double &x, double &y, double &z) const
{
  const Vector p = nodes.Get (i)->GetObject<MobilityModel> ()->GetPosition ();
  x = p.x;
  y = p.y;
  z = (i == sinkId) ? sinkAcousticDepthM : p.z;
}

void
Pipeline::SetupMedium ()
{
  NS_ABORT_MSG_IF (chanMode != "bellhop" && chanMode != "formula", "--chan must be bellhop|formula");
  MakePhy ();
  pwr = dahello::powerProfile (powerName);
  dahello::MediumConfig mc;
  mc.mac = dahello::parseMacMode (macMode);
  mc.queueCap = queueCap;
  mc.boMaxAttempts = boMax;
  mc.dataBits = dataBits;
  mc.slotBits = dataBits;
  mc.rangeM = proto->rRangeM ();
  mc.seed = static_cast<uint64_t> (seed) * 1000ull + rngRun;
  medMaxPropS = mc.rangeM / aphy->config ().c;
  med = std::make_unique<dahello::AcousticMedium> (
      *aphy, mc, nNodes (),
      [] (double d, std::function<void ()> f) { Simulator::Schedule (Seconds (d), f); },
      [] () { return Simulator::Now ().GetSeconds (); });
  med->pos = [this] (uint32_t i, double &x, double &y, double &z) { MedPos (i, x, y, z); };
  if (chanMode == "formula")
    {
      med->linkSnrDb = [this] (uint32_t s, uint32_t r) {
        double ax, ay, az, bx, by, bz;
        MedPos (s, ax, ay, az);
        MedPos (r, bx, by, bz);
        return aphy->formulaSnrDb (dahello::ChannelGate::distance3D (ax, ay, az, bx, by, bz));
      };
    }
  else
    {
      med->linkSnrDb = [this] (uint32_t s, uint32_t r) {
        double ax, ay, az, bx, by, bz;
        MedPos (s, ax, ay, az);
        MedPos (r, bx, by, bz);
        const dahello::ChannelSample cs = gate->queryLink (ax, ay, az, bx, by, bz);
        if (!cs.valid || cs.nearPressureRelease || !std::isfinite (cs.snr_db))
          {
            return -std::numeric_limits<double>::infinity ();
          }
        return cs.snr_db;
      };
    }
  med->onTxStart = [this] (uint32_t s, dahello::MediumFrame &f) { MedOnTxStart (s, f); };
  med->onTxEnd = [this] (uint32_t s, const dahello::MediumFrame &f) { MedOnTxEnd (s, f); };
  med->onDecode = [this] (uint32_t r, const dahello::MediumFrame &f, double snr) {
    MedOnDecode (r, f, snr);
  };
  med->wanted = [this] (uint32_t n, const dahello::MediumFrame &f) { return MedWanted (n, f); };
  med->onMacDrop = [this] (uint32_t n, const dahello::MediumFrame &f, int why) {
    MedOnMacDrop (n, f, why);
  };
  medArq.assign (nNodes (), {});
  medSeenU.assign (nNodes (), {});
  medHold.assign (nNodes (), {});
  medSupp.assign (nNodes (), {});
  medQd.assign (nNodes (), {});
  proto->setCtrlTxHook ([this] (uint32_t j, int cause) {
    if (!med || j >= nNodes () || !med->alive (j) || !nodeAlive (j))
      {
        return;
      }
    auto pf = std::make_shared<PFrame> ();
    pf->cause = cause;
    ++medCtrlByCause[std::min (cause, 5)];
    MedEnqueue (j, cause == 4 ? dahello::kFrameBeacon : dahello::kFrameHello, -1, helloBits, pf);
  });
  medSnap0.assign (nNodes (), 0.0);
  medSnap1.assign (nNodes (), 0.0);
  Simulator::Schedule (Seconds (std::max (0.0, warmS)), &Pipeline::MedSnapshot, this, 0);
  Simulator::Schedule (Seconds (tSim + drainS), &Pipeline::MedSnapshot, this, 1);
  MedSyncEnergy ();
  std::cout << "PORTA medium: mac=" << macMode << " chan=" << chanMode << " power=" << pwr.name
            << " per=" << aphy->config ().per << " snrReq=" << aphy->snrReqDb ()
            << " SL=" << aphy->slDb () << " NL=" << aphy->nlDb () << " rRange=" << mc.rangeM
            << " phase=" << trafficPhase << "\n";
  if (discMode == "heard")
    {
      // Plan §2.4 join announcement: one HELLO per node after U[0, 10] s.
      std::mt19937_64 rng (static_cast<uint64_t> (seed) * 0x9E3779B97F4A7C15ull + 13);
      std::uniform_real_distribution<double> u (0.0, 10.0);
      for (uint32_t j = 0; j < nNodes (); ++j)
        {
          const double delay = u (rng);
          Simulator::Schedule (Seconds (delay), [this, j] {
            if (!nodeAlive (j) || !med || !med->alive (j)) return;
            auto pf = std::make_shared<PFrame> ();
            pf->cause = 2; // bootstrap / join
            ++medCtrlByCause[2];
            MedEnqueue (j, dahello::kFrameHello, -1, helloBits, pf);
          });
        }
      std::cout << "PORTA disc=heard: join HELLO U[0,10]s per node, T_nb=" << tNbS << "\n";
    }
  if (sinkBeaconS > 0.0)
    {
      // Item 4 (amend1): the sink broadcasts its pose every sinkBeaconS (60 s), all arms.
      // First beacon at t = sinkBeaconS; frame kind beacon, cause 6, helloBits.
      // p15 A2.2(d): t = sinkBeaconPhase + sinkBeaconS*k, k >= 1 (phase 0 = p14).
      for (double t = sinkBeaconS + sinkBeaconPhase; t < tSim + drainS; t += sinkBeaconS)
        {
          Simulator::Schedule (Seconds (t), [this] {
            const uint32_t s = sinkId;
            if (!med || !med->alive (s)) return;
            auto pf = std::make_shared<PFrame> ();
            pf->cause = 6;
            ++medSinkBcnTx;
            MedEnqueue (s, dahello::kFrameBeacon, -1, helloBits, pf);
          });
        }
      std::cout << "PORTA sinkBeacon period=" << sinkBeaconS << " s (sink " << sinkId << ")\n";
    }

}

void
Pipeline::MedSnapshot (int which)
{
  std::vector<double> &v = which == 0 ? medSnap0 : medSnap1;
  std::array<double, 2 + 2 * dahello::kFrameKinds> &sp = which == 0 ? medSplit0 : medSplit1;
  sp.fill (0.0);
  for (uint32_t i = 0; i < nNodes (); ++i)
    {
      v[i] = med->energyUsed (i, pwr);
      sp[0] += pwr.idle * med->tIdle (i);
      for (int k = 0; k < dahello::kFrameKinds; ++k)
        {
          sp[2 + k] += pwr.tx * med->tTxKind (i, k);
          sp[2 + dahello::kFrameKinds + k] += pwr.rx * med->tRxKind (i, k);
        }
    }
  if (which == 1)
    {
      medStats1 = med->stats ();
      medSnapped1 = true;
    }
}

void
Pipeline::MedSyncEnergy ()
{
  // Hosts see the residual as a fraction of the battery mapped onto their own
  // E_ini scale, so their reward normalisation is unchanged.
  for (uint32_t i = 0; i < nNodes (); ++i)
    {
      if (!nodeAlive (i))
        {
          continue;
        }
      const double frac = std::max (0.0, 1.0 - med->energyUsed (i, pwr) / eBattJ);
      proto->setEnergy (i, dahello::kDefaultEiniJ * frac);
    }
}

void
Pipeline::MedEnqueue (uint32_t n, uint8_t kind, int32_t dst, int bits, std::shared_ptr<PFrame> pf,
                      bool front)
{
  dahello::MediumFrame f;
  f.dst = dst;
  f.kind = kind;
  f.bits = bits;
  f.cause = static_cast<uint8_t> (pf ? pf->cause : 0);
  f.payload = std::static_pointer_cast<void> (pf);
  med->enqueue (n, std::move (f), front);
}

void
Pipeline::MedOnTxStart (uint32_t s, dahello::MediumFrame &f)
{
  auto pf = std::static_pointer_cast<PFrame> (f.payload);
  if (!pf)
    {
      return;
    }
  pf->txStart = Simulator::Now ().GetSeconds ();
  if (f.kind == dahello::kFrameHello || f.kind == dahello::kFrameBeacon)
    {
      octrlTally.add (pf->cause, f.bits, pf->txStart, tSim); // p12: output-only counter
    }
  SyncPoses ();
  PoisonScope ps (this, s); // header = sender's own state only (I1)
  const auto &ns = proto->nodes ()[s];
  if (fcOn && s < fcSt.size () && f.kind != dahello::kFrameAck)
    {
      // p9: a pending first-contact reply is cancelled by any own state-carrying frame.
      if (pf->cause != 7 && cmp::FcCancel (fcSt[s]))
        {
          ++fcCancel;
        }
    }
  if (b6Dist && s < b6Anchor.size () && f.kind == dahello::kFrameHello)
    {
      // p9 B6: anchor = own pose at the start of every HELLO this node sends.
      b6Anchor[s] = {ns.x, ns.y, ns.z, pf->txStart};
      b6Pend[s] = 0;
    }
  if (newCmpOn && s < ownLastHello.size () && f.kind == dahello::kFrameHello)
    {
      CmpNewOnOwnHello (s, pf->txStart); // p13: inside PoisonScope(s): own state + own held rows only
    }
  if (f.kind == dahello::kFrameHello || f.kind == dahello::kFrameBeacon)
    {
      // Snapshot sender pose/energy into the payload so heard-mode receivers
      // apply the tx-time state, not a later decode-time state.
      pf->hz = ns.z;
      pf->he = proto->energy (s);
      pf->hV = proto->advertisedV (s);
      pf->hx = ns.x; pf->hy = ns.y;
      pf->hvx = ns.vx; pf->hvy = ns.vy; pf->hvz = ns.vz;
      pf->hnum = static_cast<double> (proto->advertisedNumber (s));
      pf->hG = proto->advertisedGroupAvg (s);
      if (lqarpPosDa == 2 && f.kind == dahello::kFrameHello && s < pdAdv.size ())
        {
          pdAdv[s] = dahello::PosDaPose{ns.x, ns.y, ns.z}; // p6: own pose as advertised
          pdAdvSet[s] = 1;
        }
      if (marv)
        {
          pf->hV = marv->marv ().advertisedV (s);
          pf->hVoid = marv->marv ().advertisedVoid (s);
        }
      // A2.11: refresh clock for QELAR DA (--qelarDaFix).
      proto->noteOwnCtrlTx (s);
      return;
    }
  if (f.kind != dahello::kFrameData)
    {
      return;
    }
  // header = sender's own state at transmission start
  pf->hz = ns.z;
  pf->he = proto->energy (s);
  pf->hV = proto->advertisedV (s);
  pf->hnum = static_cast<double> (proto->advertisedNumber (s));
  pf->hG = proto->advertisedGroupAvg (s);
  // A2.11 --qelarHelloOnly=1: strip neighbour attributes from QELAR data headers
  // (V, residual energy, group-average energy, depth/position, neighbour count).
  // File: scenario-porta.inc MedOnTxStart; mirrored on RX by hoSkip before applyHostFrame.
  if (qelarHelloOnly)
    {
      const double nan = std::numeric_limits<double>::quiet_NaN ();
      pf->hz = nan;
      pf->he = nan;
      pf->hV = nan;
      pf->hnum = nan;
      pf->hG = nan;
      pf->hx = nan;
      pf->hy = nan;
    }
  if (marv)
    {
      dahello::MarvProtocol &mp = marv->marv ();
      pf->hV = mp.advertisedV (s);
      pf->hVoid = mp.advertisedVoid (s);
      marv->noteStateSent (s);
      mp.noteSent (s, pf->list);
      medQd[s].erase (pf->pkt.id);
    }
  else if ((protoName == "lqarp" || protoName == "lqarp-refreshed") && !lqarpNoPosePiggyback)
    {
      // p14: Liu et al. IoT-J 13(17) Sec III-D — position in every LQARP data header.
      // z already in hz; residual energy / V / neighbour count already filled above.
      // Header *size* is not modelled per field (flat --dataBits); no bit count change.
      pf->hx = ns.x;
      pf->hy = ns.y;
    }
}

void
Pipeline::MedOnTxEnd (uint32_t s, const dahello::MediumFrame &f)
{
  auto pf = std::static_pointer_cast<PFrame> (f.payload);
  if (!pf || f.kind != dahello::kFrameData)
    {
      return;
    }
  const double T = aphy->txTime (f.bits);
  if (marv)
    {
      Simulator::Schedule (Seconds (medMaxPropS * 1.2 + T + 0.05), &Pipeline::MedMarvTxDone, this,
                           pf->pkt, pf->marvTxId);
      return;
    }
  auto it = medArq[s].find (pf->pkt.id);
  if (it == medArq[s].end ())
    {
      return;
    }
  it->second.waiting = true;
  const double wait = 2.0 * medMaxPropS * 1.2 + aphy->txTime (ackBits) + T + 1.0;
  Simulator::Schedule (Seconds (wait), &Pipeline::MedArqTimeout, this, s, pf->pkt.id,
                       it->second.gen);
}

bool
Pipeline::MedWanted (uint32_t n, const dahello::MediumFrame &f)
{
  if (!marv || f.kind != dahello::kFrameData)
    {
      return true;
    }
  auto pf = std::static_pointer_cast<PFrame> (f.payload);
  if (pf && medSupp[n].count (pf->pkt.id))
    {
      // suppressed while queued: same rule as a suppression during the hold
      ++medSuppQueued;
      ++marvSuppressed;
      medQd[n].erase (pf->pkt.id);
      MarvCopyEnd (pf->pkt, -1);
      return false;
    }
  return true;
}

void
Pipeline::MedOnMacDrop (uint32_t n, const dahello::MediumFrame &f, int why)
{
  if (why == 0)
    {
      ++nQDrop;
    }
  if (f.kind != dahello::kFrameData)
    {
      return;
    }
  auto pf = std::static_pointer_cast<PFrame> (f.payload);
  if (!pf)
    {
      return;
    }
  if (marv)
    {
      medQd[n].erase (pf->pkt.id);
      MarvCopyEnd (pf->pkt, kMarvMac);
      return;
    }
  medArq[n].erase (pf->pkt.id);
  ++nMacDrop;
  if (pf->pkt.id < inflight.size ())
    {
      inflight[pf->pkt.id] = false;
    }
}

void
Pipeline::MedUnicastSend (const Pkt &pkt, uint32_t nh, bool detour, bool staleHop)
{
  const uint32_t s = pkt.at;
  ArqSt st;
  st.pkt = pkt;
  st.nh = nh;
  st.gen = ++medArqGen;
  st.detour = detour;
  st.staleHop = staleHop;
  medArq[s][pkt.id] = st;
  auto pf = std::make_shared<PFrame> ();
  pf->pkt = pkt;
  pf->detour = detour;
  pf->staleHop = staleHop;
  MedEnqueue (s, dahello::kFrameData, static_cast<int32_t> (nh), dataBits, pf);
}

void
Pipeline::MedArqTimeout (uint32_t s, uint64_t pid, uint64_t gen)
{
  auto it = medArq[s].find (pid);
  if (it == medArq[s].end () || it->second.gen != gen)
    {
      return;
    }
  ArqSt &st = it->second;
  if (!nodeAlive (s) || !med->alive (s))
    {
      medArq[s].erase (it);
      return;
    }
  if (st.pkt.medAttempt + 1 < arqMax)
    {
      ++medRetx;
      ++st.pkt.medAttempt;
      st.gen = ++medArqGen;
      st.waiting = false;
      auto pf = std::make_shared<PFrame> ();
      pf->pkt = st.pkt;
      pf->detour = st.detour;
      pf->staleHop = st.staleHop;
      MedEnqueue (s, dahello::kFrameData, static_cast<int32_t> (st.nh), dataBits, pf);
      return;
    }
  ++nArqDrop;
  if (st.staleHop)
    {
      ++nSnrStale;
    }
  else
    {
      ++nSnrGenuine;
    }
  if (pid < inflight.size ())
    {
      inflight[pid] = false;
    }
  medArq[s].erase (it);
}

void
Pipeline::MedMarvSend (const Pkt &pkt, const std::vector<uint32_t> &list, int64_t txId)
{
  auto pf = std::make_shared<PFrame> ();
  pf->pkt = pkt;
  pf->list = list;
  pf->marvTxId = txId;
  ++marvPk[pkt.id].live; // released by MedMarvTxDone / MAC drop / queued suppression
  medQd[pkt.at][pkt.id] = pkt.parentTx;
  MedEnqueue (pkt.at, dahello::kFrameData, -1, dataBits, pf);
}

void
Pipeline::MedMarvTxDone (Pkt pkt, int64_t txId)
{
  uint32_t n = 0;
  auto it = medTxRx.find (txId);
  if (it != medTxRx.end ())
    {
      n = it->second;
      medTxRx.erase (it);
    }
  if (n == 0)
    {
      ++marvRxFail;
    }
  MarvCopyEnd (pkt, n == 0 ? kMarvSnr : -1);
}

void
Pipeline::MedOnDecode (uint32_t r, const dahello::MediumFrame &f, double snrDb)
{
  if (!nodeAlive (r))
    {
      return;
    }
  ++medRxByKind[f.kind];
  auto pf = std::static_pointer_cast<PFrame> (f.payload);
  if (!pf)
    {
      return;
    }
  if (pf->cause == 6)
    {
      ++medSinkBcnOk;
    }
  const double now = Simulator::Now ().GetSeconds ();
  const uint32_t s = f.src;
  if (discMode == "heard")
    {
      ApplyHeardFromDecode (r, s, snrDb, *pf, f.kind);
    }
  if (f.kind == dahello::kFrameAck)
    {
      if (marv)
        {
          // sink acceptance: implicit ack to prevSender, suppression of every holder
          if (pf->ackPrevSender == static_cast<int> (r))
            {
              marv->marv ().noteForwardOverheard (r, sinkId,
                                                  static_cast<uint32_t> (pf->ackPrevTop));
            }
          if (medHold[r].count (pf->ackFor) || medQd[r].count (pf->ackFor))
            {
              medSupp[r].insert (pf->ackFor);
            }
          return;
        }
      if (f.dst != static_cast<int32_t> (r))
        {
          return;
        }
      auto it = medArq[r].find (pf->ackFor);
      if (it != medArq[r].end ())
        {
          ++medAcks;
          proto->noteOwnAck (r, s); // I6: own hop acknowledged by s
          medArq[r].erase (it);
        }
      return;
    }
  if (f.kind != dahello::kFrameData)
    {
      return; // control frames: hold writes still applied by the legacy scheduler (stage M1)
    }
  if (marv)
    {
      dahello::MarvProtocol &mp = marv->marv ();
      const Pkt &pk = pf->pkt;
      MarvHeader (r, s, pf->hV, pf->hVoid, pf->he);
      if (pk.prevSender == static_cast<int> (r))
        {
          mp.noteForwardOverheard (r, s, static_cast<uint32_t> (pk.prevTop));
        }
      if (pk.parentTx >= 0)
        {
          auto h = medHold[r].find (pk.id);
          auto q = medQd[r].find (pk.id);
          if ((h != medHold[r].end () && h->second == pk.parentTx) ||
              (q != medQd[r].end () && q->second == pk.parentTx))
            {
              medSupp[r].insert (pk.id);
            }
        }
      for (uint32_t n = 1; n <= pf->list.size (); ++n)
        {
          if (pf->list[n - 1] != r)
            {
              continue;
            }
          ++medTxRx[pf->marvTxId];
          proto->noteDataReception (r, s);
          const double flight = now - pf->txStart;
          hopDelaySum += flight;
          ++nHopDelay;
          Pkt next = pk;
          next.at = r;
          next.hops = pk.hops + 1;
          next.prevSender = static_cast<int> (s);
          next.prevTop = static_cast<int> (pf->list.front ());
          next.pos = n;
          next.tRecv = now;
          next.parentTx = pf->marvTxId;
          ++marvPk[pk.id].live;
          ++marvCopies;
          Simulator::ScheduleNow (&Pipeline::MarvReceive, this, next);
          break;
        }
      return;
    }
  if (f.dst != static_cast<int32_t> (r))
    {
      return;
    }
  // unicast: always acknowledge (a lost ACK makes the sender retransmit)
  auto ack = std::make_shared<PFrame> ();
  ack->ackFor = pf->pkt.id;
  MedEnqueue (r, dahello::kFrameAck, static_cast<int32_t> (s), ackBits, ack, true);
  if (!medSeenU[r].insert (pf->pkt.id).second)
    {
      ++medDupRx;
      return;
    }
  if (pf->staleHop)
    {
      ++nStaleAdmitOk;
    }
  proto->noteDataReception (r, s);
  const double flight = now - pf->txStart;
  hopDelaySum += flight;
  ++nHopDelay;
  Pkt next = pf->pkt;
  next.at = r;
  next.hops = pf->pkt.hops + 1;
  next.viaStale = pf->pkt.viaStale || pf->staleHop;
  next.medAttempt = 0;
  if (pf->detour)
    {
      next.detourHops = pf->pkt.detourHops + 1;
    }
  Simulator::ScheduleNow (&Pipeline::OnArrive, this, next);
}

void
Pipeline::MedNoteDelivery (const Pkt &pkt)
{
  const double now = Simulator::Now ().GetSeconds ();
  if (pkt.genTime >= warmS && pkt.genTime < tSim)
    {
      ++medDelW;
      medLatW += now - pkt.genTime;
      medHopW += pkt.hops;
    }
}

void
Pipeline::WriteMediumReport (std::ostringstream &oss) const
{
  if (!med)
    {
      return;
    }
  const dahello::MediumStats &S = medSnapped1 ? medStats1 : med->stats ();
  oss << "MEDIUM_BEGIN\n";
  oss << std::setprecision (10);
  oss << "mac=" << macMode << " chan=" << chanMode << " power=" << pwr.name
      << " per=" << aphy->config ().per << " snr_req_db=" << aphy->snrReqDb ()
      << " sl_db=" << aphy->slDb () << " nl_db=" << aphy->nlDb ()
      << " r_range_m=" << med->config ().rangeM << " phase=" << trafficPhase
      << " warm_s=" << warmS << "\n";
  for (int k = 0; k < dahello::kFrameKinds; ++k)
    {
      oss << "frames kind=" << dahello::frameKindName (k) << " tx=" << S.txByKind[k]
          << " bits=" << S.bitsByKind[k] << " ok=" << S.okByKind[k]
          << " coll_dataint=" << S.collByKind[k][0] << " coll_ctrlint=" << S.collByKind[k][1]
          << " noise=" << S.noiseByKind[k] << " busy_miss=" << S.busyMissByKind[k]
          << " hd_loss=" << S.hdLossByKind[k] << " hd_abort=" << S.hdAbortByKind[k] << "\n";
    }
  const double tEnd = tSim + drainS;
  oss << "mac_stats rx_starts=" << S.rxStarts << " cs_attempts=" << S.csAttempts
      << " cs_busy=" << S.csBusy << " mac_drop=" << S.macDrop << " q_drop=" << S.qDrop
      << " unwanted=" << S.unwanted << " busy_time_s=" << S.busyTime
      << " utilisation=" << S.busyTime / std::max (1e-9, tEnd) << "\n";
  oss << "ctrl_by_cause link=" << medCtrlByCause[0] << " periodic=" << medCtrlByCause[1]
      << " bootstrap=" << medCtrlByCause[2] << " hoststate=" << medCtrlByCause[3]
      << " ebeacon=" << medCtrlByCause[4] << " adaptive=" << medCtrlByCause[5] << "\n";
  oss << "arq retx=" << medRetx << " acks=" << medAcks << " arq_drop=" << nArqDrop
      << " mac_drop_data=" << nMacDrop << " qdrop_all=" << nQDrop << " dup_rx=" << medDupRx
      << " dup_at_sink=" << medDupAtSink << " marv_supp_queued=" << medSuppQueued << "\n";
  const double pdrW = medGenW ? static_cast<double> (medDelW) / medGenW : 0.0;
  oss << "window gen=" << medGenW << " delivered=" << medDelW << " pdr=" << pdrW
      << " lat_mean_s=" << (medDelW ? medLatW / medDelW : 0.0)
      << " hops_mean=" << (medDelW ? medHopW / medDelW : 0.0) << "\n";
  double eW = 0.0, eRun = 0.0;
  for (uint32_t i = 0; i < nNodes (); ++i)
    {
      const double e1 = medSnapped1 ? medSnap1[i] : med->energyUsed (i, pwr);
      eW += e1 - medSnap0[i];
      eRun += e1;
    }
  oss << "energy_J run=" << eRun << " window=" << eW << "\n";
  const auto &a = medSplit0;
  const auto &b = medSnapped1 ? medSplit1 : medSplit0;
  oss << "energy_split_window_J idle=" << b[0] - a[0];
  for (int k = 0; k < dahello::kFrameKinds; ++k)
    {
      oss << " tx_" << dahello::frameKindName (k) << "=" << b[2 + k] - a[2 + k];
    }
  for (int k = 0; k < dahello::kFrameKinds; ++k)
    {
      oss << " rx_" << dahello::frameKindName (k) << "="
          << b[2 + dahello::kFrameKinds + k] - a[2 + dahello::kFrameKinds + k];
    }
  oss << "\n";
  if (sinkBeaconS > 0.0)
    {
      // energy of beacon-kind frames = sink beacon + energy beacon (cause 4); ebeacon is listed in ctrl_by_cause
      oss << "sink_beacon period_s=" << sinkBeaconS << " tx=" << medSinkBcnTx << " rx_ok=" << medSinkBcnOk
          << " E_tx_beacon_window_J=" << b[2 + dahello::kFrameBeacon] - a[2 + dahello::kFrameBeacon]
          << " E_rx_beacon_window_J=" << b[2 + dahello::kFrameKinds + dahello::kFrameBeacon] - a[2 + dahello::kFrameKinds + dahello::kFrameBeacon]
          << " ebeacon_frames=" << medCtrlByCause[4] << "\n";
    }
  oss << "ebit_total_mJ_per_bit window=" << (medDelW ? 1e3 * eW / (medDelW * double (dataBits)) : 0.0)
      << "\n";
  if (discMode == "heard")
    {
      // DIAG: T_nb expiry exposure. over_T = gaps (second frame in the window) longer than T.
      const double hrs = (tSim - warmS) / 3600.0;
      oss << "heard_gaps first_contacts=" << hgFirst << " gaps=" << hgGaps << " gaps_auv=" << hgGapsAuv
          << " max_gap_s=" << hgMaxGap << " window_h=" << hrs;
      const char *nm[4] = {"80", "1800", "3600", "7200"};
      for (int q = 0; q < 4; ++q)
        {
          oss << " over_" << nm[q] << "=" << hgOver[q] << " over_auv_" << nm[q] << "=" << hgOverAuv[q];
        }
      oss << " tnb_in_use_lqarp_s=" << proto->heardTnbS () << "\n";
    }
  if (lifetimeOut)
    {
      // p2, output only: projected lifetime under the run's own energy model. No battery is
      // drained in the simulation (eBattJ is not enforced), so nothing here changes dynamics.
      // For each AUV alive at report time: mean power P_i = E_i / t_alive_i; T_i = eBattJ / P_i.
      // FND = min T_i, HND = median T_i (over AUVs alive at report time; removed AUVs excluded
      // and counted). in_run_deaths = AUVs whose E_i already >= eBattJ.
      std::vector<double> T;
      uint32_t removed = 0, deaths = 0;
      for (uint32_t i = 0; i < nNodes (); ++i)
        {
          if (i == sinkId)
            {
              continue;
            }
          const double e = med->energyUsed (i, pwr);
          if (e >= eBattJ)
            {
              ++deaths;
            }
          if (!med->alive (i))
            {
              ++removed;
              continue;
            }
          const double ta = med->tAlive (i);
          if (ta > 0.0 && e > 0.0)
            {
              T.push_back (eBattJ * ta / e);
            }
        }
      std::sort (T.begin (), T.end ());
      const double fnd = T.empty () ? 0.0 : T.front ();
      const double hnd = T.empty () ? 0.0 : (T.size () % 2 ? T[T.size () / 2] : 0.5 * (T[T.size () / 2 - 1] + T[T.size () / 2]));
      oss << "LIFETIME_PROJ ebatt_J=" << eBattJ << " n_alive=" << T.size () << " n_removed=" << removed
          << " in_run_deaths=" << deaths << " fnd_s=" << fnd << " hnd_s=" << hnd
          << " fnd_h=" << fnd / 3600.0 << " hnd_h=" << hnd / 3600.0 << "\n";
    }
  if (lqarpPosDa == 2)
    {
      oss << "LQARP_POSDA mode=2 margin=" << lqarpPosDaMargin << " evals=" << pdEvals
          << " mem_events=" << pdMem << " rank_events=" << pdRank
          << " rank_deferred=" << pdRankDeferred << " skip_nonfinite=" << pdSkipNonfinite
          << " hello_cause3=" << medCtrlByCause[3] << "\n";
    }
  if (b6Dist || b8NaiveB || b9FcOnly || fcOn)
    {
      oss << "COMPARATOR arm=" << (b6Dist ? "B6" : b8NaiveB ? "B8" : b9FcOnly ? "B9" : "P") << " fc_sched=" << fcSched
          << " fc_tx=" << fcTx << " fc_cancel_piggyback=" << fcCancel << " fc_ratelimited=" << fcRateLim
          << " b6_evals=" << b6Evals << " b6_dist=" << b6Dist1 << " b6_depth=" << b6Depth << " b6_tmax=" << b6TmaxN
          << " b8_Tr=" << b8Tr << " b6_dth=" << b6Dth << " b6_zth=" << b6Zth << " b6_Tmax=" << b6Tmax << "\n";
    }
  if (newCmpOn)
    {
      oss << "COMPARATOR13 arm=" << (b2Trickle ? "B2" : b3Tap ? "B3" : b4Speed ? "B4" : "P") << " tx=" << n13Tx
          << " evals=" << n13Evals << " trickle_resets=" << trResets << " trickle_suppressed=" << trSuppressed
          << " trickle_consistent=" << trCons << " trickle_incons=" << trIncons
          << " b2Imin=" << b2Imin << " b2d=" << b2Doublings << " b2k=" << b2K << " b2incons=" << b2Incons
          << " b3Ropt=" << b3Ropt << " b4A=" << b4A << " daRefresh=" << daRefresh << "\n";
    }
  oss << "MEDIUM_END\n";
}


void
Pipeline::ApplyHeardFromDecode (uint32_t r, uint32_t s, double snrDb, const PFrame &pf, uint8_t kind)
{
  // Heard-only: r learns s only from fields that this frame actually carries.
  const double now = Simulator::Now ().GetSeconds ();
  if (fcOn && kind != dahello::kFrameAck && r < fcSt.size ())
    {
      // p9 B8/B9: reactive first-contact reply (plan §2.4). Evaluated BEFORE this frame
      // writes r's hold: the test is r's OWN held record of s (fresh within T_nb).
      // --leakTest=10 swaps in the legacy live range gate (oracle) to prove the guard.
      // p10: never-heard-only (user decision 2026-10-16): reply iff r has never held a
      // record of s (MarvRecord::heard is set by the first decoded frame and never cleared).
      // A2.11: under --qelarDaFix, QELAR uses Fwd::everHeard (heard_ bit, never cleared).
      bool known = false;
      if (leakTest == 10)
        {
          NS_ABORT_MSG_IF (!marv, "--leakTest=10 needs MARV");
          known = marv->marv ().inRange (r, s);
        }
      else if (marv)
        {
          known = marv->marv ().record (r, s).heard;
        }
      else
        {
          known = proto->everHeard (r, s);
        }
      std::uniform_real_distribution<double> u (0.0, 1.0);
      const double u01 = u (fcRng);
      const double before = fcSt[r].lastTx + 5.0;
      const double t = cmp::FcOnDecode (fcSt[r], known, now, u01, 2.0, 5.0);
      if (std::isfinite (t))
        {
          ++fcSched;
          if (t == before && t > now + 2.0)
            {
              ++fcRateLim;
            }
          Simulator::Schedule (Seconds (t - now), &Pipeline::CmpFcFire, this, r, fcSt[r].token);
        }
    }
  // p15 A2.2(b) helloOnlyHost; A2.11 qelarHelloOnly: data carries no neighbour attrs for QELAR.
  const bool hoSkip = (helloOnlyHost || qelarHelloOnly) && kind == dahello::kFrameData;
  if (newCmpOn && kind != dahello::kFrameAck && r < chNew.size () && r != s && hoSkip)
    {
      ++rwCmpDataSkipped; // p15: comparator table not written from data under --helloOnlyHost=1
    }
  if (newCmpOn && kind != dahello::kFrameAck && r < chNew.size () && r != s && !hoSkip)
    {
      // p13: r's OWN held table (decoded frames only). New/re-appearing id = table change.
      if (kind == dahello::kFrameData) { ++rwCmpData; } else { ++rwCmpCtrl; } // p15 output-only
      const size_t idx = static_cast<size_t> (r) * nNodes () + s;
      const bool wasFresh = chT[idx] >= 0.0 && now - chT[idx] <= tNbS;
      if (!wasFresh)
        {
          chNew[r] += 1.0;
          chTabChg[r] = 1;
        }
      else if (b2Trickle && (kind == dahello::kFrameHello || kind == dahello::kFrameBeacon))
        {
          ++trCons;
          cmp::TrickleConsistent (trSt[r]); // RFC 6206 4.2(3): consistent transmission heard
        }
      chT[idx] = now;
      chFresh[idx] = 1;
      if (kind == dahello::kFrameHello || kind == dahello::kFrameBeacon)
        {
          chVx[idx] = pf.hvx; chVy[idx] = pf.hvy; chVz[idx] = pf.hvz; // tx-time snapshot in the frame
        }
    }
  if (hoSkip)
    {
      ++rwDataSkipped; // p15 A2.2(b): data header writes nothing (porta.inc p14 :902-929 -> :934/:954/:958/:963)
      return;
    }
  if (kind != dahello::kFrameAck)
    {
      // DIAG: gap since the previous hold-refreshing frame r <- s (no behaviour effect)
      const uint32_t nn = nNodes ();
      if (hgLast.size () != static_cast<size_t> (nn) * nn)
        {
          hgLast.assign (static_cast<size_t> (nn) * nn, -1.0);
        }
      double &last = hgLast[static_cast<size_t> (r) * nn + s];
      if (last < 0.0)
        {
          ++hgFirst;
        }
      else if (now >= warmS && now < tSim)
        {
          static const double thr[4] = {80.0, 1800.0, 3600.0, 7200.0};
          const double g = now - last;
          const bool auv = (r != sinkId && s != sinkId);
          ++hgGaps;
          hgGapsAuv += auv ? 1 : 0;
          hgMaxGap = std::max (hgMaxGap, g);
          for (int q = 0; q < 4; ++q)
            {
              if (g > thr[q])
                {
                  ++hgOver[q];
                  hgOverAuv[q] += auv ? 1 : 0;
                }
            }
        }
      last = now;
    }
  const double nan = std::numeric_limits<double>::quiet_NaN ();
  double x = nan, y = nan, z = nan, vx = nan, vy = nan, vz = nan, e = nan, num = nan, V = nan;
  if (kind == dahello::kFrameHello || kind == dahello::kFrameBeacon)
    {
      // Full advertised vector + pose. Sender's own state is allowed information.
      // tx-time snapshot carried in the frame (no decode-time read of s's state)
      x = pf.hx; y = pf.hy; z = pf.hz;
      vx = pf.hvx; vy = pf.hvy; vz = pf.hvz;
      e = pf.he;
      V = pf.hV;
      num = pf.hnum;
    }
  else if (kind == dahello::kFrameData)
    {
      z = pf.hz;
      e = pf.he;
      V = pf.hV;
      num = pf.hnum; // tx-time count (header field)
      V = pf.hV;
      // p14: Liu III-D LQARP data header carries position, residual energy, V, neighbour count.
      // MARV / QELAR / SAQR decode paths unchanged (guards below).
      if (marv)
        {
          // MARV header: V, void, energy, depth already in pf; XY not applied here (unchanged).
        }
      else if ((protoName == "lqarp" || protoName == "lqarp-refreshed") && !lqarpNoPosePiggyback)
        {
          // Corrected LQARP (default): refresh held pose + energy + V + number from data header.
          x = pf.hx;
          y = pf.hy;
          // z, e, V, num already set from pf above
        }
      else
        {
          // Pre-fix LQARP (--lqarpNoPosePiggyback=1) and non-LQARP hosts (QELAR/SAQR/…):
          // no position/energy via applyHeardHold from data (legacy / host-specific paths).
          z = nan;
          e = nan;
        }
    }
  else
    {
      ++rwAckDec;
      return; // ACK: no hold
    }
  ++rwDec[kind < 4 ? kind : 3]; // p15 output-only: decode reaching the record writes, by kind
  proto->applyHeardHold (r, s, now, x, y, z, vx, vy, vz, e, snrDb, num, V);
  {
    // QELAR / SAQR held write (amend1 I2 + v3 correction: V in their data headers)
    dahello::HeardFrame hf;
    hf.kind = kind;
    hf.snrDb = snrDb;
    hf.V = pf.hV;
    if (kind == dahello::kFrameHello || kind == dahello::kFrameBeacon)
      {
        hf.poseCarried = true;
        hf.x = pf.hx; hf.y = pf.hy; hf.z = pf.hz;
        hf.vx = pf.hvx; hf.vy = pf.hvy; hf.vz = pf.hvz;
        hf.energy = pf.he; hf.groupAvg = pf.hG; hf.number = pf.hnum;
      }
    else
      {
        // data header: QELAR = E, group avg, V; SAQR = E, number, V (host keeps its fields)
        hf.energy = pf.he; hf.groupAvg = pf.hG; hf.number = pf.hnum;
      }
    PoisonScope ps (this, r);
    proto->applyHostFrame (r, s, now, hf);
  }
  if (marv)
    {
      marv->applyMarvHeard (r, s, now, V, pf.hVoid, e, z, vx, vy, vz,
                            kind == dahello::kFrameHello || kind == dahello::kFrameBeacon);
    }
  if (auto *sc = const_cast<dahello::DahelloProtocol *> (proto->scheduler ()))
    {
      sc->applyHeardHold (r, s, now, x, y, z, vx, vy, vz, e, snrDb, num, V);
    }
}

void
Pipeline::InstallLeakTest ()
{
  // F4 positive control: a deliberate leak must make the process exit with code 3.
  // Refused in tuning/eval by the run harness; allowed here for the unit suite.
  std::cout << "LEAKTEST planting k=" << leakTest << " (expect exit 3)\n";
  switch (leakTest)
    {
    // Real positive controls (amend1 F4): each case only RE-ENABLES one legacy oracle
    // path inside the host; nothing here exits. The process must be stopped by the
    // guard (oracle-guard.h, exit 3) when the host takes that path in heard mode.
    case 7: // p8: marv-dahello-hs host-state HELLO delivered via the live link gate
    case 8: // p8: marv-dahello-hs daFire reads the true peer depth
      if (!marv || !marv->hostState ())
        {
          std::cerr << "LEAKTEST " << leakTest << " needs --protocol=marv-dahello-hs[-energy]\n";
          std::exit (2);
        }
      marv->setLeakPlant (leakTest);
      break;
    case 9:  // p9: B6 trigger reads a peer pose (poisoned) instead of its own
    case 10: // p9: B8/B9 first-contact test via the legacy live range gate
      if (!marv || (leakTest == 9 && !b6Dist) || (leakTest == 10 && !b8NaiveB && !b9FcOnly && protoName != "marv-dahello-hs"))
        {
          std::cerr << "LEAKTEST " << leakTest << " needs --b6Dist=1 (9) or --b8NaiveB/--b9FcOnly=1 (10)\n";
          std::exit (2);
        }
      break; // planted in CmpB6Tick / ApplyHeardFromDecode
    case 11: // p13: B4 SpeedOpt reads peer TRUE velocities (poisoned) instead of held ones
      if (!marv || !b4Speed)
        {
          std::cerr << "LEAKTEST 11 needs --b4Speed=1\n";
          std::exit (2);
        }
      break; // planted in CmpNewOnOwnHello
    case 1: // MARV qualifies() reads the true z_j
    case 3: // MARV neighbour set from the true range test
      if (!marv)
        {
          std::cerr << "LEAKTEST " << leakTest << " needs a marv-* protocol\n";
          std::exit (2);
        }
      marv->setLeakPlant (leakTest);
      break;
    case 2: // LQARP reward reads the live linkSample
      proto->setLeakLiveLink (true);
      break;
    case 4: // SAQR candidateSet reads the true neighbour count
      if (proto->name ().compare (0, 4, "saqr") != 0)
        {
          std::cerr << "LEAKTEST 4 needs a saqr protocol\n";
          std::exit (2);
        }
      proto->setLeakPlant (4);
      break;
    case 5: // bin_tt: true peer pose written into a held record inside the tick scope
      if (!proto->scheduler ())
        {
          std::cerr << "LEAKTEST 5 needs a scheduler host (periodic|trickle|speedadapt|marv-*|--hostSched)\n";
          std::exit (2);
        }
      tt::plant = 1;
      break;
    case 6: // p7: true (live) peer pose read inside the --lqarpPosDa=2 evaluation scope
      if (lqarpPosDa != 2 || protoName != "lqarp")
        {
          std::cerr << "LEAKTEST 6 needs --lqarpPosDa=2 --protocol=lqarp\n";
          std::exit (2);
        }
      break; // planted in LqarpPosDaTick
    default:
      std::cerr << "LEAKTEST unknown k=" << leakTest << "\n";
      std::exit (2);
    }
}

void
Pipeline::InstallComparators ()
{
  // p9: B6 / B8 / B9 (strengthening_plan.md §3, §2.4; prereg :59, :176, :217-219). Off by default.
  const int nOn = (b6Dist ? 1 : 0) + (b8NaiveB ? 1 : 0) + (b9FcOnly ? 1 : 0)
                  + (b2Trickle ? 1 : 0) + (b3Tap ? 1 : 0) + (b4Speed ? 1 : 0);
  // p10: P (marv-dahello-hs, heard) always carries the shared first-contact reply.
  const bool pArm = (protoName == "marv-dahello-hs" && discMode == "heard");
  NS_ABORT_MSG_IF (pArm && nOn != 0, "p10: --b6Dist/--b8NaiveB/--b9FcOnly do not apply to marv-dahello-hs");
  NS_ABORT_MSG_IF (!pArm && nOn != 1, "p9: use exactly one of --b6Dist / --b8NaiveB / --b9FcOnly");
  NS_ABORT_MSG_IF (!medOn () || discMode != "heard", "p9 comparators need the shared medium and --disc=heard");
  NS_ABORT_MSG_IF (!marv, "p9 comparators need a MARV host");
  NS_ABORT_MSG_IF (b8NaiveB && protoName != "marv-naive", "--b8NaiveB needs --protocol=marv-naive");
  NS_ABORT_MSG_IF ((b2Trickle || b3Tap || b4Speed) && protoName != "marv-ctrl-off",
                   "--b2Trickle / --b3Tap / --b4Speed need --protocol=marv-ctrl-off");
  NS_ABORT_MSG_IF (daRefresh != 0.0 && !pArm, "--daRefresh applies to marv-dahello-hs (heard) only");
  NS_ABORT_MSG_IF (!(daRefresh >= 0.0), "--daRefresh must be >= 0");
  NS_ABORT_MSG_IF ((b6Dist || b9FcOnly) && protoName != "marv-ctrl-off",
                   "--b6Dist / --b9FcOnly need --protocol=marv-ctrl-off (host alone + the comparator rule)");
  const uint32_t n = nNodes ();
  if (b8NaiveB || b9FcOnly || pArm)
    {
      fcOn = true;
      fcSt.assign (n, cmp::FcState{});
      fcRng.seed (static_cast<uint64_t> (seed) * 0xD1B54A32D192ED03ull + 29);
    }
  if (b8NaiveB)
    {
      const double tr = cmp::B8RefreshS (b8Tr);
      NS_ABORT_MSG_IF (std::isnan (tr), "--b8Tr must be 0 (infinity) or in [60,300] s");
      NS_ABORT_MSG_IF (std::isfinite (naiveRefresh), "--b8NaiveB sets T_r via --b8Tr; do not pass --naiveRefresh");
      marv->setNaiveRefresh (tr);
    }
  if (b6Dist)
    {
      NS_ABORT_MSG_IF (!(b6Dth > 0.0 && b6Zth > 0.0 && b6Tmax > 0.0 && b6CheckS > 0.0), "p9 B6 parameters must be > 0");
      b6Anchor.assign (n, {std::numeric_limits<double>::quiet_NaN (), 0.0, 0.0, -1.0});
      b6Pend.assign (n, 0);
      for (double t = b6CheckS; t < tSim + drainS + 1e-12; t += b6CheckS)
        {
          Simulator::Schedule (Seconds (t), &Pipeline::CmpB6Tick, this);
        }
    }
  if (b2Trickle || b3Tap || b4Speed || daRefresh > 0.0)
    {
      NS_ABORT_MSG_IF (b2Trickle && !(b2Imin > 0.0 && b2Doublings >= 0 && b2Doublings <= 16 && b2K >= 0
                                     && (b2Incons == "table" || b2Incons == "host")),
                       "p13 B2 parameters invalid");
      NS_ABORT_MSG_IF (b3Tap && !(b3Ropt > 0.0), "p13 --b3Ropt must be > 0");
      NS_ABORT_MSG_IF (b4Speed && !(b4A > 0.0), "p13 --b4A must be > 0");
      newCmpOn = true;
      const size_t nn = static_cast<size_t> (n) * n;
      chT.assign (nn, -1.0);
      chVx.assign (nn, std::numeric_limits<double>::quiet_NaN ());
      chVy = chVx; chVz = chVx;
      chFresh.assign (nn, 0);
      chTabChg.assign (n, 0);
      chNew.assign (n, 0.0);
      chOwnVoid.assign (n, -1);
      nfPend.assign (n, 0);
      nfF.assign (n, 1.0 / 60.0); // TAP initial frequency (OUR CHOICE; paper unspecified)
      nfNext.assign (n, std::numeric_limits<double>::infinity ());
      ownLastHello.assign (n, -1.0);
      trSt.assign (n, cmp::TrickleState{});
      nfRng.seed (static_cast<uint64_t> (seed) * 0x9E3779B97F4A7C15ull + 31);
      for (double t = 1.0; t < tSim + drainS + 1e-12; t += 1.0)
        {
          Simulator::Schedule (Seconds (t), &Pipeline::CmpNewTick, this);
        }
      std::cout << "PORTA p13 comparator " << (b2Trickle ? "B2" : b3Tap ? "B3" : b4Speed ? "B4" : "P")
                << " b2Imin=" << b2Imin << " b2d=" << b2Doublings << " b2k=" << b2K << " b2incons=" << b2Incons
                << " b3Ropt=" << b3Ropt << " b4A=" << b4A << " daRefresh=" << daRefresh << " fc=" << fcOn << "\n";
      return;
    }
  std::cout << "PORTA p9 comparator " << (pArm ? "P" : b6Dist ? "B6" : b8NaiveB ? "B8" : "B9")
            << " fc=" << fcOn << " b8Tr=" << b8Tr << " b6Dth=" << b6Dth << " b6Zth=" << b6Zth
            << " b6Tmax=" << b6Tmax << " b6CheckS=" << b6CheckS << " fcJitter=2 fcGap=5\n";
}

void
Pipeline::CmpFcFire (uint32_t r, uint64_t tok)
{
  if (!nodeAlive (r) || !med || !med->alive (r))
    {
      return;
    }
  const double now = Simulator::Now ().GetSeconds ();
  if (!cmp::FcFire (fcSt[r], tok, now))
    {
      return;
    }
  ++fcTx;
  auto pf = std::make_shared<PFrame> ();
  pf->cause = 7; // first-contact reply
  MedEnqueue (r, dahello::kFrameHello, -1, helloBits, pf);
}

void
Pipeline::CmpB6Tick ()
{
  // p9 B6: each AUV compares its OWN pose with its OWN pose at its last HELLO, inside
  // PoisonScope(j) (peer copies poisoned). --leakTest=9 reads a peer pose instead.
  SyncPoses ();
  const uint32_t n = nNodes ();
  const double now = Simulator::Now ().GetSeconds ();
  for (uint32_t j = 0; j < n; ++j)
    {
      if (j == sinkId || !nodeAlive (j) || !med->alive (j) || b6Pend[j] || !std::isfinite (b6Anchor[j][0]))
        {
          continue; // no anchor until the join HELLO went out
        }
      PoisonScope ps (this, j);
      uint32_t k = j;
      if (leakTest == 9)
        {
          k = (j + 1) % n;
          if (k == sinkId)
            {
              k = (k + 1) % n;
            }
        }
      const auto &a = proto->nodes ()[k];
      const auto &an = b6Anchor[j];
      ++b6Evals;
      const int c = cmp::B6Trigger (a.x, a.y, a.z, an[0], an[1], an[2], an[3], now, b6Dth, b6Zth, b6Tmax);
      if (c < 0)
        {
          dahello::oracleLeak ("B6:pose", j, k);
        }
      if (c == 0)
        {
          continue;
        }
      (c == 1 ? b6Dist1 : c == 2 ? b6Depth : b6TmaxN)++;
      b6Pend[j] = 1;
      auto pf = std::make_shared<PFrame> ();
      pf->cause = 8; // B6 distance trigger
      MedEnqueue (j, dahello::kFrameHello, -1, helloBits, pf);
    }
}

void
Pipeline::InstallLqarpPosDa ()
{
  // Gate 2: LQARP position-aware membership event (plan §2.3). Default off.
  // Every tickDt, each node j checks whether its live pose changed its membership
  // in any held upstream i's N_range ∩ N_SNR ∩ N_depth vs the pose it last
  // advertised. On a change, j enqueues one HELLO (rate-limited by tNb-style Imin).
  NS_ABORT_MSG_IF (protoName != "lqarp" && protoName != "dahello",
                   "--lqarpPosDa needs protocol=lqarp|dahello");
  NS_ABORT_MSG_IF (lqarpPosDa == 2 && protoName != "lqarp",
                   "--lqarpPosDa=2 needs protocol=lqarp (LQARP host held records)");
  NS_ABORT_MSG_IF (lqarpPosDa != 1 && lqarpPosDa != 2, "--lqarpPosDa must be 0, 1 or 2");
  NS_ABORT_MSG_IF (!medOn (), "--lqarpPosDa requires the shared medium");
  std::cout << "PORTA lqarpPosDa=" << lqarpPosDa << " Tnb=" << tNbS << "\n";
  if (lqarpPosDa == 2)
    {
      const uint32_t nn = nNodes ();
      pdAdv.assign (nn, dahello::PosDaPose{});
      pdAdvSet.assign (nn, 0);
      pdI.assign (nn, 5.0);   // I_min (decision_aware_design.md §3)
      pdH.assign (nn, -1e18);
      pdPending.assign (nn, 0);
    }
  for (double t = tickDt; t < tSim + drainS + 1e-12; t += tickDt)
    {
      Simulator::Schedule (Seconds (t), &Pipeline::LqarpPosDaTick, this);
    }
}

void
Pipeline::LqarpPosDaTick ()
{
  if (lqarpPosDa == 2)
    {
      // p6: LQARP position-aware membership/rank test (model/lqarp-posda.h). Evaluated at
      // each advertiser j from OWN state + j's HELD records only, inside PoisonScope(j)
      // (peer node copies poisoned), so any oracle read would be NaN/±1e9 and caught.
      // Membership events fire immediately (I <- I_min); rank events follow the Trickle-style
      // back-off of decision_aware_design.md §3 (I_min 5 s, I_max 80 s, deferred if t < h).
      SyncPoses ();
      const uint32_t n = nNodes ();
      const double now = Simulator::Now ().GetSeconds ();
      dahello::PosDaParams q;
      q.rRangeM = proto->rRangeM ();
      q.eIni = dahello::kDefaultEiniJ;          // = cfg.eIni set in BuildProtocol
      q.gamma = dahello::LqarpConfig{}.gamma;   // LQARP host gamma (BuildProtocol leaves the default)
      q.margin = lqarpPosDaMargin;
      for (uint32_t j = 1; j < n; ++j) // the sink does not move
        {
          if (!med->alive (j) || !pdAdvSet[j])
            continue;
          dahello::PosDaResult res;
          {
            PoisonScope ps (this, j);
            const auto &nj = proto->nodes ()[j];
            dahello::PosDaInput in;
            in.self = j;
            in.adv = pdAdv[j];
            in.now = dahello::PosDaPose{nj.x, nj.y, nj.z};
            in.eSelf = proto->energy (j);
            in.vSelf = proto->advertisedV (j);
            in.numSelf = static_cast<double> (proto->advertisedNumber (j));
            for (uint32_t i = 0; i < n; ++i)
              {
                if (i == j)
                  continue;
                dahello::LqarpProtocol::HeldView hv;
                if (!proto->peekHeld (j, i, hv) || !hv.heard)
                  continue;
                dahello::PosDaRec r;
                r.id = i; r.x = hv.x; r.y = hv.y; r.z = hv.z; r.energy = hv.energy;
                r.V = hv.V; r.number = hv.number; r.fresh = hv.fresh;
                in.recs.push_back (r);
              }
            if (leakTest == 6 && !in.recs.empty ())
              {
                // PLANTED LEAK (p7): replace the first held record's pose by that peer's LIVE
                // protocol-copy pose (poisoned NaN/±1e9 inside PoisonScope(j)).
                const auto &live = proto->nodes ()[in.recs.front ().id];
                in.recs.front ().x = live.x;
                in.recs.front ().y = live.y;
                in.recs.front ().z = live.z;
              }
            // p7 guard: every pose fed to the test must be a finite, plausible held/own value.
            // Held records are written only from finite decoded fields (applyHeardHold drops
            // non-finite), so NaN or |x| > 1e7 m here can only come from a poisoned live read.
            {
              auto bad = [] (double v) { return !std::isfinite (v) || std::fabs (v) > 1.0e7; };
              if (bad (in.now.x) || bad (in.now.y) || bad (in.now.z))
                dahello::oracleLeak ("LqarpPosDa:ownPose", j, j);
              for (const auto &rr : in.recs)
                if (bad (rr.x) || bad (rr.y) || bad (rr.z))
                  dahello::oracleLeak ("LqarpPosDa:heldPose", j, rr.id);
            }
            res = dahello::EvaluateLqarpPosDa (in, q);
          }
          ++pdEvals;
          pdSkipNonfinite += res.nSkipPose + res.nSkipEnergy + res.nSkipScore + (res.selfNonfinite ? 1 : 0);
          bool send = false;
          if (res.membership)
            {
              ++pdMem;
              pdI[j] = 5.0;
              pdH[j] = now + pdI[j];
              pdPending[j] = 0;
              send = true;
            }
          else if (res.rank || pdPending[j])
            {
              if (res.rank && !pdPending[j])
                ++pdRank;
              if (!res.rank)
                {
                  pdPending[j] = 0; // re-test against the current reference: no longer relevant
                }
              else if (now < pdH[j])
                {
                  pdPending[j] = 1;
                  ++pdRankDeferred;
                }
              else
                {
                  pdPending[j] = 0;
                  pdI[j] = std::min (2.0 * pdI[j], 80.0);
                  pdH[j] = now + pdI[j];
                  send = true;
                }
            }
          if (send)
            {
              auto pf = std::make_shared<PFrame> ();
              pf->cause = 3; // host-state / position DA
              ++medCtrlByCause[3];
              MedEnqueue (j, dahello::kFrameHello, -1, helloBits, pf);
            }
        }
      return;
    }
  if (!med || discMode != "heard" && discMode != "oracle")
    {
      // Position DA can run under oracle too (legacy holds); membership uses
      // held upstream poses vs own live pose.
    }
  SyncPoses ();
  static std::vector<double> lastAdvX, lastAdvY, lastAdvZ, lastFire;
  const uint32_t n = nNodes ();
  if (lastAdvX.size () != n)
    {
      lastAdvX.assign (n, std::numeric_limits<double>::quiet_NaN ());
      lastAdvY.assign (n, std::numeric_limits<double>::quiet_NaN ());
      lastAdvZ.assign (n, std::numeric_limits<double>::quiet_NaN ());
      lastFire.assign (n, -1e18);
    }
  const double now = Simulator::Now ().GetSeconds ();
  const double r = proto->rRangeM ();
  for (uint32_t j = 1; j < n; ++j) // skip sink
    {
      if (!nodeAlive (j) || !med->alive (j)) continue;
      if (now - lastFire[j] < 5.0) continue; // I_min = 5 s
      const auto &nj = proto->nodes ()[j];
      bool fire = false;
      // Compare current vs last-advertised membership against each held upstream.
      // Under oracle, "held" is the frozen first-contact pose; under heard, the
      // last decoded HELLO/init pose.
      // Simplified membership: range to i using i's held pose of... wait, j evaluates
      // using its table entries for upstream i (held pose of i) — i.e. frozen_[j][i].
      // We approximate via peekFilter when available.
      for (uint32_t i = 0; i < n; ++i)
        {
          if (i == j || !nodeAlive (i)) continue;
          dahello::FilterPeek pk;
          if (!proto->peekFilter (j, i, pk) || !pk.heard) continue;
          // Distance under last advertised pose of j vs current pose of j, to held i.
          // peekFilter doesn't expose held pose of i; use SyncPoses nodes_[i] as a
          // stand-in only under oracle. Under heard, i's pose in nodes_ is i's own
          // (allowed as "known from last HELLO" only if we stored it — applyHeardHold
          // wrote frozen, not nodes_). For Gate 2 scaffolding we use live i only when
          // disc=oracle; when heard we fire on own displacement > 0.25 R_op since last HELLO.
          (void)r;
          break;
        }
      // Displacement trigger (membership proxy for scaffolding): fire if j moved
      // > 0.25 * R_op since last advertisement.
      if (std::isfinite (lastAdvX[j]))
        {
          const double d = std::sqrt ((nj.x - lastAdvX[j]) * (nj.x - lastAdvX[j])
                                      + (nj.y - lastAdvY[j]) * (nj.y - lastAdvY[j])
                                      + (nj.z - lastAdvZ[j]) * (nj.z - lastAdvZ[j]));
          if (d > 0.25 * phyRopM) fire = true;
        }
      else
        {
          // First tick: record pose, no fire (join HELLO is separate).
          lastAdvX[j] = nj.x; lastAdvY[j] = nj.y; lastAdvZ[j] = nj.z;
        }
      if (!fire) continue;
      lastFire[j] = now;
      lastAdvX[j] = nj.x; lastAdvY[j] = nj.y; lastAdvZ[j] = nj.z;
      auto pf = std::make_shared<PFrame> ();
      pf->cause = 3; // host-state / position DA
      ++medCtrlByCause[3];
      MedEnqueue (j, dahello::kFrameHello, -1, helloBits, pf);
    }
}


void
Pipeline::CmpNewOnOwnHello (uint32_t s, double now)
{
  // p13: called at the start of every HELLO node s transmits, inside PoisonScope(s).
  ownLastHello[s] = now;
  nfPend[s] = 0;
  const uint32_t n = nNodes ();
  if (b3Tap || b4Speed)
    {
      ++n13Evals;
      double next;
      if (b3Tap)
        {
          double size = 0.0;
          for (uint32_t j = 0; j < n; ++j)
            {
              const size_t idx = static_cast<size_t> (s) * n + j;
              size += (chT[idx] >= 0.0 && now - chT[idx] <= tNbS) ? 1.0 : 0.0;
            }
          const double f = cmp::TapUpdate (nfF[s], chNew[s], size, b3Ropt, 1.0 / 300.0, 1.0 / 5.0);
          if (std::isnan (f))
            {
              dahello::oracleLeak ("B3:f", s, s);
            }
          nfF[s] = f;
          chNew[s] = 0.0;
          next = now + 1.0 / f;
        }
      else
        {
          const auto &me = proto->nodes ()[s]; // own velocity (I1)
          double sum = 0.0, cnt = 0.0;
          uint32_t kLeak = s;
          for (uint32_t j = 0; j < n; ++j)
            {
              const size_t idx = static_cast<size_t> (s) * n + j;
              if (j == s || !(chT[idx] >= 0.0 && now - chT[idx] <= tNbS))
                {
                  continue;
                }
              double vx = chVx[idx], vy = chVy[idx], vz = chVz[idx];
              if (leakTest == 11)
                {
                  const auto &pj = proto->nodes ()[j]; // planted oracle read (poisoned peer copy)
                  vx = pj.vx; vy = pj.vy; vz = pj.vz;
                  kLeak = j;
                }
              else if (!std::isfinite (vx))
                {
                  continue; // no velocity held yet (data-header-only record)
                }
              const double dx = me.vx - vx, dy = me.vy - vy, dz = me.vz - vz;
              sum += std::sqrt (dx * dx + dy * dy + dz * dz);
              cnt += 1.0;
            }
          const double S = cnt > 0.0 ? sum / cnt : 0.0;
          const double f = cmp::SpeedOptF (S, b4A, phyRopM, 1.0 / 300.0, 1.0 / 5.0);
          if (std::isnan (f))
            {
              dahello::oracleLeak ("B4:vel", s, kLeak);
            }
          next = now + 1.0 / f;
        }
      nfNext[s] = next;
    }
}

void
Pipeline::CmpNewTick ()
{
  // p13: every 1 s each AUV evaluates its rule from OWN state + OWN held rows, inside
  // PoisonScope(j) (peer node copies poisoned).
  SyncPoses ();
  const uint32_t n = nNodes ();
  const double now = Simulator::Now ().GetSeconds ();
  std::uniform_real_distribution<double> u (0.0, 1.0);
  for (uint32_t j = 0; j < n; ++j)
    {
      if (j == sinkId || !nodeAlive (j) || !med->alive (j))
        {
          continue;
        }
      PoisonScope ps (this, j);
      for (uint32_t s = 0; s < n; ++s)
        {
          const size_t idx = static_cast<size_t> (j) * n + s;
          if (chFresh[idx] && now - chT[idx] > tNbS)
            {
              chFresh[idx] = 0; // expiry of an own held entry = table change
              chTabChg[j] = 1;
            }
        }
      bool send = false;
      if (b2Trickle)
        {
          if (ownLastHello[j] < 0.0)
            {
              continue; // starts after the node's join HELLO
            }
          auto &st = trSt[j];
          if (!st.init)
            {
              cmp::TrickleInit (st, b2Imin, b2Doublings, b2K, now, u (nfRng));
              chTabChg[j] = 0;
              chOwnVoid[j] = marv->marv ().advertisedVoid (j) ? 1 : 0;
            }
          bool incons;
          if (b2Incons == "table")
            {
              incons = chTabChg[j] != 0;
            }
          else
            {
              const int v = marv->marv ().advertisedVoid (j) ? 1 : 0; // own advertised state (I1)
              incons = (v != chOwnVoid[j]);
              chOwnVoid[j] = v;
            }
          chTabChg[j] = 0;
          if (incons)
            {
              ++trIncons;
              trResets += cmp::TrickleInconsistent (st, now, u (nfRng)) ? 1 : 0;
            }
          const bool due = !st.sent && now >= st.t;
          send = cmp::TrickleStep (st, now, u (nfRng));
          if (due && !send)
            {
              ++trSuppressed;
            }
        }
      else if (b3Tap || b4Speed)
        {
          send = now >= nfNext[j];
        }
      else if (daRefresh > 0.0)
        {
          send = ownLastHello[j] >= 0.0 && now - ownLastHello[j] >= daRefresh;
        }
      if (!send || nfPend[j])
        {
          continue;
        }
      nfPend[j] = 1;
      if (b3Tap || b4Speed)
        {
          nfNext[j] = std::numeric_limits<double>::infinity (); // re-armed at the HELLO tx start
        }
      ++n13Tx;
      auto pf = std::make_shared<PFrame> ();
      pf->cause = 9; // p13 comparator / P refresh HELLO
      MedEnqueue (j, dahello::kFrameHello, -1, helloBits, pf);
    }
}
