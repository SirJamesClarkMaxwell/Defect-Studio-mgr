"""Test suite for include direction checker."""

import unittest
from pathlib import Path
import sys
import json
import tempfile
import shutil

# Add parent dir to path so we can import check_include_directions
sys.path.insert(0, str(Path(__file__).parent.parent))

try:
    from check_include_directions import CheckIncludeDirections, Rule
except ImportError:
    # Module doesn't exist yet; tests should fail to import
    CheckIncludeDirections = None
    Rule = None


class TestCheckIncludeDirections(unittest.TestCase):
    """Test cases for include direction checker."""

    def setUp(self):
        """Create a temporary directory for test files."""
        if CheckIncludeDirections is None:
            self.skipTest("check_include_directions module not found")
        self.test_dir = tempfile.mkdtemp()

    def tearDown(self):
        """Clean up temporary directory."""
        if hasattr(self, "test_dir"):
            shutil.rmtree(self.test_dir, ignore_errors=True)

    def create_test_file(self, path: str, content: str):
        """Create a test file with given content."""
        full_path = Path(self.test_dir) / path
        full_path.parent.mkdir(parents=True, exist_ok=True)
        full_path.write_text(content)

    def create_rules(self, allowed: dict, exceptions: list = None):
        """Create rules dict for testing."""
        return {
            "allowed": allowed,
            "known_exceptions": exceptions or []
        }

    def test_allowed_edge_passes(self):
        """Test 1: allowed edge (Domain -> Core) should pass."""
        if CheckIncludeDirections is None:
            self.skipTest("Module not available")

        # Create test file
        self.create_test_file("src/Domain/Test.hpp", '#include "Core/Foo.hpp"')

        # Create rules allowing Domain -> Core
        rules = self.create_rules({
            "Domain": ["Core", "Domain"],
            "Core": ["Core"]
        })

        checker = CheckIncludeDirections(Path(self.test_dir) / "src", rules)
        violations = checker.check()

        # Should have no violations
        self.assertEqual(len(violations), 0, f"Expected no violations, got {violations}")

    def test_disallowed_edge_detected(self):
        """Test 2: disallowed edge (Domain -> Renderer) should be detected."""
        if CheckIncludeDirections is None:
            self.skipTest("Module not available")

        self.create_test_file("src/Domain/Test.hpp", '#include "Renderer/Foo.hpp"')

        rules = self.create_rules({
            "Domain": ["Core", "Domain"],
            "Renderer": ["Core", "Renderer"]
        })

        checker = CheckIncludeDirections(Path(self.test_dir) / "src", rules)
        violations = checker.check()

        # Should have exactly 1 violation
        self.assertEqual(len(violations), 1)
        self.assertIn("Domain", violations[0])
        self.assertIn("Renderer", violations[0])

    def test_known_exception_skipped(self):
        """Test 3: known exception edges should not be reported."""
        if CheckIncludeDirections is None:
            self.skipTest("Module not available")

        self.create_test_file("src/Events/Test.hpp", '#include "Renderer/Foo.hpp"')

        rules = self.create_rules(
            {"Events": ["Core"], "Renderer": ["Core", "Renderer"]},
            [{"from": "Events", "to": "Renderer", "reason": "known cycle"}]
        )

        checker = CheckIncludeDirections(Path(self.test_dir) / "src", rules)
        violations = checker.check()

        # Should have no violations (exception was applied)
        self.assertEqual(len(violations), 0)

    def test_non_module_includes_ignored(self):
        """Test 4: non-module includes (<vector>, etc.) should be ignored."""
        if CheckIncludeDirections is None:
            self.skipTest("Module not available")

        self.create_test_file(
            "src/Domain/Test.hpp",
            '#include <vector>\n#include <string>\n#include "Core/Foo.hpp"'
        )

        rules = self.create_rules({
            "Domain": ["Core", "Domain"],
            "Core": ["Core"]
        })

        checker = CheckIncludeDirections(Path(self.test_dir) / "src", rules)
        violations = checker.check()

        # Should have no violations (only non-module includes + allowed include)
        self.assertEqual(len(violations), 0)


if __name__ == "__main__":
    unittest.main()
