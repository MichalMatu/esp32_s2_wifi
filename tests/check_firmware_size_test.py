#!/usr/bin/env python3
import importlib.util
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "scripts" / "check_firmware_size.py"
SPEC = importlib.util.spec_from_file_location("check_firmware_size", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class ParseSizeTests(unittest.TestCase):
    def test_parses_platformio_summary(self) -> None:
        output = (
            "RAM:   [=         ]  14.3% (used 46860 bytes from 327680 bytes)\n"
            "Flash: [=======   ]  72.5% (used 1140916 bytes from 1572864 bytes)\n"
        )
        self.assertEqual(
            MODULE.parse_size(output),
            {
                "used": {"ram_bytes": 46860, "flash_bytes": 1140916},
                "totals": {"ram_bytes": 327680, "flash_bytes": 1572864},
            },
        )

    def test_parses_ansi_and_comma_separated_numbers(self) -> None:
        output = (
            "\x1b[32mRAM:\x1b[0m [=] 14.3% "
            "(used 46,860 bytes from 327,680 bytes)\n"
            "Flash: [=======] 72.5% "
            "(used 1,140,916 bytes from 1,572,864 bytes)\n"
        )
        measured = MODULE.parse_size(output)
        self.assertEqual(measured["used"]["ram_bytes"], 46860)
        self.assertEqual(measured["used"]["flash_bytes"], 1140916)

    def test_rejects_missing_resource_summary(self) -> None:
        with self.assertRaisesRegex(ValueError, "flash_bytes"):
            MODULE.parse_size(
                "RAM: [=] 14.3% (used 46860 bytes from 327680 bytes)\n"
            )

    def test_budget_accepts_known_good_measurement(self) -> None:
        budget = {
            "baseline": {"ram_bytes": 46860, "flash_bytes": 1140916},
            "limits": {"ram_bytes": 55052, "flash_bytes": 1173684},
            "totals": {"ram_bytes": 327680, "flash_bytes": 1572864},
        }
        measured = {
            "used": {"ram_bytes": 46860, "flash_bytes": 1140916},
            "totals": {"ram_bytes": 327680, "flash_bytes": 1572864},
        }
        self.assertTrue(MODULE.check_budget(budget, measured))

    def test_budget_rejects_flash_over_limit(self) -> None:
        budget = {
            "baseline": {"ram_bytes": 46860, "flash_bytes": 1140916},
            "limits": {"ram_bytes": 55052, "flash_bytes": 1173684},
            "totals": {"ram_bytes": 327680, "flash_bytes": 1572864},
        }
        measured = {
            "used": {"ram_bytes": 46860, "flash_bytes": 1173685},
            "totals": {"ram_bytes": 327680, "flash_bytes": 1572864},
        }
        self.assertFalse(MODULE.check_budget(budget, measured))


if __name__ == "__main__":
    unittest.main()
