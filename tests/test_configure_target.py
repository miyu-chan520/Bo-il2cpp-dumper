import tempfile
import unittest
from pathlib import Path

from scripts.configure_target import configure_package, validate_package_name


class ValidatePackageNameTests(unittest.TestCase):
    def test_accepts_typical_android_application_ids(self):
        for name in ("com.example.app", "com.example.game_2", "org.Example.app"):
            with self.subTest(name=name):
                validate_package_name(name)

    def test_rejects_invalid_or_shell_like_values(self):
        invalid = (
            "",
            "com",
            ".com.example",
            "com..example",
            "com.1example.app",
            "com.example; touch /tmp/pwned",
            'com.example.app"; echo unsafe',
            "com.example\nother.value",
            "com.example-foo.app",
        )
        for name in invalid:
            with self.subTest(name=name):
                with self.assertRaises(ValueError):
                    validate_package_name(name)

    def test_rejects_package_names_over_maximum_length(self):
        name = "a." + ("b" * 254)
        with self.assertRaises(ValueError):
            validate_package_name(name)


class ConfigurePackageTests(unittest.TestCase):
    def _make_project(self, root: Path):
        (root / "module/src/main/cpp").mkdir(parents=True)
        (root / "module.gradle").write_text(
            'ext {\n    moduleDescription = "Il2CppDumper Zygisk version."\n}\n',
            encoding="utf-8",
        )
        (root / "module/src/main/cpp/game.h").write_text(
            '#define GamePackageName "com.game.packagename"\n',
            encoding="utf-8",
        )

    def test_replaces_expected_placeholders(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self._make_project(root)
            configure_package(root, "com.example.demo")
            gradle = (root / "module.gradle").read_text(encoding="utf-8")
            header = (root / "module/src/main/cpp/game.h").read_text(encoding="utf-8")
            self.assertIn('moduleDescription = "(com.example.demo) Il2CppDumper Zygisk version."', gradle)
            self.assertIn('#define GamePackageName "com.example.demo"', header)
            self.assertNotIn("com.game.packagename", header)

    def test_invalid_name_does_not_modify_files(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self._make_project(root)
            before_gradle = (root / "module.gradle").read_text(encoding="utf-8")
            before_header = (root / "module/src/main/cpp/game.h").read_text(encoding="utf-8")
            with self.assertRaises(ValueError):
                configure_package(root, "com.example; touch /tmp/pwned")
            self.assertEqual(before_gradle, (root / "module.gradle").read_text(encoding="utf-8"))
            self.assertEqual(before_header, (root / "module/src/main/cpp/game.h").read_text(encoding="utf-8"))

    def test_missing_placeholder_fails_before_writing(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self._make_project(root)
            (root / "module.gradle").write_text("unexpected content\n", encoding="utf-8")
            before_header = (root / "module/src/main/cpp/game.h").read_text(encoding="utf-8")
            with self.assertRaises(ValueError):
                configure_package(root, "com.example.demo")
            self.assertEqual(before_header, (root / "module/src/main/cpp/game.h").read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
