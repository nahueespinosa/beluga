# Additional tooling

Repository-level tools for generating code coverage reports and static analysis for Beluga.

1. **Build project, run tests, and generate code coverage report**. You will find a report in `./lcov/index.html`.
   ```bash
   cd /ws
   ./src/beluga/tools/build-and-test.sh
   ```

1. **Run static analysis tools**.
   ```bash
   cd /ws
   ./src/beluga/tools/run-clang-tidy.sh
   ```
