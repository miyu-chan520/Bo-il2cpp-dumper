# Contributing

Thanks for helping maintain the project.

## Before opening a pull request

1. Explain the problem and the expected behavior.
2. Keep each change focused and document compatibility assumptions.
3. Run the checks below and include their results:
   ```sh
   python -m unittest discover -s tests -v
   python -m compileall -q scripts tests
   ```
4. Do not include private application data, metadata dumps, credentials, signing keys, or device identifiers.
5. Preserve third-party license and copyright notices.

## Scope and safety

Contributions should support maintainability, build reliability, clear diagnostics, and authorized testing. Do not add code or instructions for anti-cheat evasion, unauthorized access, or defeating protection mechanisms in third-party applications. Changes to native runtime behavior should include a clear rationale and tests that can be run without proprietary target data.

## Pull request checklist

- [ ] The purpose and scope are explained.
- [ ] Automated checks pass, or failures are documented.
- [ ] Build and compatibility claims are supported by actual test results.
- [ ] No secrets or proprietary artifacts are included.
