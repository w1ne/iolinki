# iolinki Documentation

Comprehensive documentation for the iolinki IO-Link Device Stack.

## Getting Started

- **[README](../README.md)** - Project overview and quick start
- **[INSTALL](../INSTALL.md)** - Installation instructions
- **[VISION](VISION.md)** - Project vision and goals

## Development

- **[ROADMAP](ROADMAP.md)** - Development roadmap and V1.1.5 compliance checklist
- **[ARCHITECTURE](ARCHITECTURE.md)** - System architecture and design
- **[CODING_STANDARDS](CODING_STANDARDS.md)** - Code style and conventions
- **[RELEASE_STRATEGY](RELEASE_STRATEGY.md)** - Release process and versioning

## Technical Guides

- **[API Reference](API.md)** - Complete API documentation
- **[Frame Support](FRAMES.md)** - Supported M-sequences and frame structures
- **[Testing Guide](TESTING.md)** - Unit tests, integration tests, and Virtual Master
- **[Platform Porting](PORTING.md)** - How to port to new platforms and RTOSes

## Tools

- **[Virtual Master](../tools/virtual_master/README.md)** - Python Virtual IO-Link Master
  - [Services](../tools/virtual_master/SERVICES.md) - Supported services and API

## Specification

- **[IO-Link Interface Spec V1.1.5](IOL-Interface-Spec_10002_V1.1.5_Oct2025/)** - Official specification

## API Documentation (Doxygen)

## Continuous Integration

| Platform | Status |
|----------|--------|
| **Linux** | ✅ Verified |
| **Zephyr** | ✅ Verified (Native Sim) |
| **Bare Metal** | ✅ Verified (Build) |
| **Code Quality** | ✅ Pass (MISRA/Cppcheck) |

## Quick Start

### Run Tests (Docker)
```bash
docker build -t iolinki-test .
docker run --rm iolinki-test
```

Output: `docs/html/index.html`

## Contributing

See [CODING_STANDARDS](CODING_STANDARDS.md) for contribution guidelines.
