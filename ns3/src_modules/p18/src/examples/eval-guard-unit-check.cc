// eval-guard-unit-check.cc — isolated test of portaguard::CheckEvalGuard (p3).
// Never runs a simulation and never touches the shipped registry path.
#include "eval-guard.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>
using namespace portaguard;
static int fails = 0, total = 0;
static void Check (bool ok, const char *name, const std::string &detail = "")
{
  ++total;
  if (!ok) ++fails;
  std::printf ("%s %s%s%s\n", ok ? "PASS" : "FAIL", name, detail.empty () ? "" : " :: ", detail.c_str ());
}
static void Write (const std::string &p, const std::string &s)
{
  std::ofstream f (p, std::ios::binary);
  f << s;
}
int main ()
{
  // SHA-256 NIST vectors
  Check (Sha256Hex ("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "sha256 empty");
  Check (Sha256Hex ("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256 abc");
  Check (Sha256Hex ("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
             == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "sha256 448-bit");
  Check (Sha256Hex (std::string (1000000, 'a'))
             == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "sha256 1e6 x 'a'");

  char tmpl[] = "/tmp/evalguard_XXXXXX";
  const std::string dir = mkdtemp (tmpl);
  const std::string add = dir + "/addendum.md", reg = dir + "/registry.sha256",
                    bad = dir + "/bad.sha256", junk = dir + "/junk.sha256";
  const std::string body = "tuning addendum (test fixture only)\nn_eval=TBD\n";
  Write (add, body);
  Write (reg, Sha256Hex (body) + "  addendum.md\n");
  Write (bad, std::string (64, '0') + "\n");
  Write (junk, "not-a-hash\n");

  // default / non-eval path: silent accept
  auto r = CheckEvalGuard (20261121u, false, "", "");
  Check (r.accept && !r.evalSeed && r.reason.empty (), "tuning seed 20261121, eval=0 -> silent accept");
  // Literal amend1 A6(d): ">= 20261201" includes the 202691xx-202693xx burned diagnostic blocks.
  r = CheckEvalGuard (20269101u, false, "", "");
  Check (!r.accept && r.evalSeed, "burned-block seed 20269101 is numerically >= 20261201 -> REFUSE (literal A6(d))", r.reason);
  r = CheckEvalGuard (20260921u, false, "", kDefaultRegistry);
  Check (r.accept && r.reason.empty (), "legacy seed 20260921, eval=0 -> silent accept");
  r = CheckEvalGuard (20261200u, false, "", "");
  Check (r.accept && !r.evalSeed, "boundary 20261200 is not an eval seed");
  // refusals
  r = CheckEvalGuard (20261201u, false, "", "");
  Check (!r.accept && r.evalSeed, "20261201 eval=0 -> REFUSE", r.reason);
  r = CheckEvalGuard (20269999u, false, add, reg);
  Check (!r.accept, "eval seed, addendum given but eval=0 -> REFUSE", r.reason);
  r = CheckEvalGuard (20261201u, true, "", reg);
  Check (!r.accept, "eval=1 no addendum -> REFUSE", r.reason);
  r = CheckEvalGuard (20261201u, true, dir + "/missing.md", reg);
  Check (!r.accept, "eval=1 unreadable addendum -> REFUSE", r.reason);
  r = CheckEvalGuard (20261201u, true, add, dir + "/missing.sha256");
  Check (!r.accept, "eval=1 unreadable registry -> REFUSE", r.reason);
  r = CheckEvalGuard (20261201u, true, add, junk);
  Check (!r.accept, "eval=1 malformed registry -> REFUSE", r.reason);
  r = CheckEvalGuard (20261201u, true, add, bad);
  Check (!r.accept, "eval=1 sha mismatch -> REFUSE", r.reason);
  Write (dir + "/tampered.md", body + " ");
  r = CheckEvalGuard (20261201u, true, dir + "/tampered.md", reg);
  Check (!r.accept, "eval=1 tampered addendum (1 byte) -> REFUSE", r.reason);
  r = CheckEvalGuard (20261112u, true, add, reg);
  Check (!r.accept && !r.evalSeed, "eval=1 with tuning seed 20261112 -> REFUSE", r.reason);
  // accept path (logic only; nothing is simulated)
  r = CheckEvalGuard (20261201u, true, add, reg);
  Check (r.accept && r.addendumSha == Sha256Hex (body), "eval=1 + matching addendum -> ACCEPT", r.reason);
  Write (dir + "/upper.sha256", [&] { std::string h = Sha256Hex (body); for (auto &c : h) c = std::toupper (c); return h; } ());
  r = CheckEvalGuard (20261201u, true, add, dir + "/upper.sha256");
  Check (r.accept, "registry hex is case-insensitive -> ACCEPT");
  Check (kEvalGuardExit == 4 && kEvalSeedMin == 20261201u, "constants (exit 4, min seed 20261201)");

  std::system (("rm -rf " + dir).c_str ());
  std::printf ("EVAL_GUARD_UNIT %d/%d %s\n", total - fails, total, fails ? "FAIL" : "PASS");
  return fails ? 1 : 0;
}
