# Commercial source scope and provenance

The **same `develop` source tree** is used for open-source and commercial
releases. The alternative commercial licence is a separate grant for the
**specific source snapshot and product family named in the signed agreement**.
It is not a grant over every file or historical contribution in the GitHub
repository.

## Included production files

Subject to the signed commercial agreement and final rights review, the source
set for a company device-stack integration consists of:

- `src/` and `include/iolinki/`: IO-Link Device runtime and public interfaces;
- agreed owner-authored reference port(s), for example
  `examples/stm32g0_tiol112/` and `examples/reference_device/`;
- the relevant licence, build and integration instructions.

One source tree is maintained. No separate commercial fork is required.

## Separate licence material

- `tests/`, Python conformance scripts and CI assets remain available in the
  public repository under their applicable licences. They are **not** included
  in the proprietary sublicensing grant unless the signed agreement identifies
  them as cleared.
- Official IO-Link specification PDFs under `docs/` are reference documents,
  not owned by the project and not part of a commercial source delivery.
- Third-party components retain their original licences. The IODDForge CRC
  helper in `tools/vendor/ioddforge/` is MIT-licensed and has its own notice.
  It is not required for the runtime delivery.

## Earlier external contributions

Merged external PRs #45 (which incorporated changes from #44/#46) and #48
precede the contributor agreement introduced by PR #53. The existing public
GPLv3 grant for those contributions is not withdrawn. PR #54 replaces their
substantive runtime implementation with independently developed code and new
specification-based regression tests, while preserving repository history.

As a provenance signal, `git blame` of the draft PR #54 runtime source still
attributes a few **generic fragments** (for example closing braces, `return`,
a state assignment and index lookup) to those earlier commits. Git line
attribution alone does not establish authorship of the independent replacement
or a copyright licence. Review the complete functions and source history;
**do not use a clean blame report as a substitute for rights review**.

The proprietary licence **must not** be represented as covering older GPL-only
contributed tests or other material without a documented separate right.
Future external contributions require explicit acceptance of the commercial
licensing terms in the submitting PR, as enforced on `develop`.

## Release acceptance

Before commercial source is delivered, record the exact commit hash, product
family, included paths, third-party licence notices, and approval of source
provenance. Run the full CI and the agreed target-specific checks. Simulator and
cross-compiler evidence does not establish the customer's real C/Q `tA`, IAR
9.70 build, or certification.
