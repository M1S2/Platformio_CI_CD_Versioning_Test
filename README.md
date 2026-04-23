# Platformio_CI_CD_Versioning_Test

Testproject for CI/CD pipeline for platformio.

## Steps to copy to another Repo
- Copy .github/workflow/createRelease.yaml and .github/workflow/createRollingDevRelease.yaml
- Step "Patch version into code" in the workflow expects a specific marker and version file:
  - Create a Part1/Software/include/version.h file that has the following content (define value must match): #define FW_VERSION "@VERSION@"
