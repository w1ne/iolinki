# iolinki and the EU Cyber Resilience Act

*This page is orientation for device makers evaluating the stack. It is not legal
advice; your conformity assessment is yours.*

## You remain the manufacturer

Integrating `iolinki` — or any third-party stack — does not change your status
under Regulation (EU) 2024/2847 (the Cyber Resilience Act). CE marking, the EU
Declaration of Conformity, the ten-year technical-documentation retention, and the
Article 14 reporting obligations for your product stay entirely with you. What a
stack supplier owes you is the *foundation* your technical documentation builds
on. That is exactly what this package is.

Key dates: the CRA's vulnerability-reporting obligations apply from
**11 September 2026**; the full obligations from **11 December 2027**.

## What iolinki provides

| Deliverable | Where | Terms |
|---|---|---|
| SBOM per release (CycloneDX 1.6 + SPDX 2.3) | attached to every [tagged release](https://github.com/w1ne/iolinki/releases) | free, public |
| STRIDE threat model aligned to IO-Link guideline 10.512 | [`docs/security/THREAT_MODEL.md`](THREAT_MODEL.md) | free, public |
| Coordinated disclosure + advisory process | [`SECURITY.md`](../../SECURITY.md) | free, public |
| CRA compliance statement mapping the stack to Regulation (EU) 2024/2847 Annex I, issued per stack release for your product context | commercial license package | commercial |
| Contractually agreed security updates over a defined support period (default five years) | commercial license package | commercial |

The public artifacts let you verify our engineering rigor before you talk to us.
The commercial artifacts are the contract-grade documents your CRA technical file
and your supplier-management process need — the same package other stack vendors
have announced for late 2026, available from us now (pending final legal review of
the statement wording).

## Why a protocol stack is in scope at all

Commercially licensed software placed on the EU market is a "product with digital
elements" under the CRA. As the stack's supplier we carry manufacturer obligations
for the stack itself — which is why the SBOM, the disclosure process, and the
support-period commitment exist as maintained artifacts rather than sales
material. The stack is in the CRA's *default* (non-critical) class: conformity is
self-assessed, no notified body involved.

## The division of labor, concretely

**We cover (for the stack):** risk analysis of the stack's attack surface
(threat model), input-validation and integrity mechanisms with code anchors,
SBOM, vulnerability handling and advisories, security fixes over the support
period.

**You cover (for your device):** your product risk assessment, firmware update
mechanism and its authenticity verification (the stack does not include the
BLOB Transfer & Firmware Update profile — see the threat model's gaps section),
boot integrity, physical-protection guidance in your user documentation, your
DoC, CE marking, and Article 14 reporting.

For commercial-package inquiries, use the contact on the maintainer's GitHub
profile or open a (non-security) discussion on the repository.
