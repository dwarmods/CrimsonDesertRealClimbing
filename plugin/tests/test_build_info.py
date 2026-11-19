"""Offline checks of the fail-closed build table (no game files needed)."""
import pathlib, re, unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
HDR = (ROOT / "src" / "build_info.h").read_text()
INI = (ROOT / "ClimbmodASI.ini").read_text()


def builds():
    out = {}
    for m in re.finditer(r"inline constexpr BuildDef (k\d+) \{(.*?)\n    \};", HDR, re.S):
        body = m.group(2)
        nums = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]+)u?", body)]
        rtti = re.findall(r'"(\.\?AV[^"]+)"', body)
        out[m.group(1)] = dict(nums=nums, rtti=rtti, label=re.search(r'"([^"]+)"', body).group(1))
    return out


class BuildTable(unittest.TestCase):
    def test_version_matches_ini(self):
        v = re.search(r'kVersion = "([^"]+)"', HDR).group(1)
        self.assertIn(f"Climbmod ASI {v}", INI.splitlines()[0])

    def test_known_builds(self):
        b = builds()
        self.assertEqual(set(b), {"k3663", "k3732"})
        order = re.search(r"kBuilds\[\] = \{([^}]*)\}", HDR).group(1)
        self.assertEqual([s.strip(" &") for s in order.split(",")], ["k3732", "k3663"])

    def test_each_build_complete_and_distinct(self):
        b = builds()
        ids = set()
        for name, d in b.items():
            # identity(3) + 6 vtables + 5 slots + owner offset
            self.assertEqual(len(d["nums"]), 3 + 6 + 5 + 1, name)
            self.assertEqual(len(d["rtti"]), 6, name)
            self.assertTrue(all(r.endswith("@pa@@") for r in d["rtti"]), name)
            ids.add(tuple(d["nums"][:3]))
            self.assertEqual(d["nums"][-1], 0x18, name)
            slots = d["nums"][9:14]
            self.assertEqual(len(set(slots)), 5, name)
        self.assertEqual(len(ids), len(b), "two builds share a PE identity")

    def test_rtti_names_identical_across_builds(self):
        b = builds()
        self.assertEqual(b["k3663"]["rtti"], b["k3732"]["rtti"])

    def test_diag_defaults_never_modify_game(self):
        for key in ("AnimSwap", "KeepAxesDrawn"):
            self.assertRegex(INI, rf"(?m)^{key}=0\s*$")

    def test_ini_keys_are_read_by_plugin(self):
        src = (ROOT / "src" / "climbdiag.cpp").read_text()
        for key in re.findall(r"(?m)^([A-Za-z]+)=", INI):
            self.assertIn(f'L"{key}"', src, f"INI key {key} is not read by the plugin")

    def test_default_log_level_is_targeted(self):
        self.assertRegex(INI, r"(?m)^LogLevel=1\s*$")


if __name__ == "__main__":
    unittest.main()
