# AI Policy

This repository permits unrestricted use of AI and LLM-based tools, including for committing, pushing, and publishing.

- [AI Policy](#ai-policy)
  - [TL;DR](#tldr)
  - [Definitions](#definitions)
  - [Scope](#scope)
  - [Introduction](#introduction)
  - [Requirements](#requirements)
  - [Sign-off, Authorship, and Copyright](#sign-off-authorship-and-copyright)
  - [Permitted Usage](#permitted-usage)
  - [Forbidden Usage](#forbidden-usage)
  - [Codebase Ingestion](#codebase-ingestion)
  - [Enforcement](#enforcement)
  - [References](#references)

## TL;DR

AI-assisted contributions are fully permitted in this repository.

- AI agents are free to submit PRs, author commits, write commit messages, and interact with the codebase.
- Low-quality contributions, regardless of authorship, will be reworked or rejected.

## Definitions

An *AI or LLM-based tool* is any piece of software that generates code, text, or other content through machine learning models. This includes, but is not limited to: chat assistants, agentic coding tools/harnesses, inline code completion suggestions you have accepted into the code, and AI-driven explanation/refactoring features.

For the purposes of this policy, basic deterministic/static development tools (such as clang-tidy, git, compilers, etc.) are not considered AI tools.

Terms such as "substantially", "large-scale", "majority", and "low-quality" are intentionally left vague, and will be interpreted at the maintainers' discretion. When in doubt, ask first.

## Scope

This policy applies to all code contributions, alongside issues and pull requests surrounding this codebase. This includes:

- Commit messages
- PR descriptions
- Code review comments
- Issue/PR comments
- Patches submitted via e-mail

Areas that are not directly related to code--such as our surrounding communities--are not covered by the terms of this policy. Additionally, spelling/grammar checking via AI or LLM-based tools is not covered under this document.

## Introduction

In the right hands, AI and LLM-based tools are incredibly useful for getting features and bugfixes out the door as fast as possible. We are therefore open to AI-assisted contributions, including work where an AI agent was responsible for the majority of the implementation.

The goal of this document is *not* to set arbitrary restrictions, but rather to keep our development healthy and stable, while still allowing contributors to use the tools they find helpful. A contribution must be worth more to the project than the time it takes to review it.

Throughout this document, **must** and **must not** are inflexible, binding requirements. Refusal to follow these is treated as a violation of the terms outlined in this document.

## Requirements

These apply to all contributions, whether AI-assisted or not.

1. *Review*. All code **must** be thoroughly reviewed before opening a PR or marking it ready for review.
2. *Testing*. All code changes **must** be tested before submission.
3. *Quality*. All submitted code is held to a strict standard of excellence.
4. *Licensing*. All code **must** be compatible with the GPLv3 or later. You **must** warrant that you have the right to submit it under those terms, and that it was not copied from work published under an incompatible license.

## Sign-off, Authorship, and Copyright

AI agents are fully permitted to author changes, write commit messages, and add `Signed-off-by:`, `Co-authored-by:`, or `Co-developed-by:` tags. AI agents may commit and push directly to this repository on behalf of the maintainer.

## Permitted Usage

This is a non-exhaustive list of permitted uses of AI and LLM-based tools.

- Drafting issues and pull requests.
- Debugging issues, bugs, glitches, and crashes, whether pre-existing or introduced by changes.
- Reviewing changes before submitting a PR.
- Research, brainstorming, and codebase searching/understanding.
- Applying tedious but not particularly large changes, such as mass-migrating specific classes to polyfills or abstractions.
- Generating boilerplate/scaffolding for new code.
- Writing unit tests.
- Large-scale refactors driven by an AI agent.
- Committing, pushing, and publishing releases directly.

## Forbidden Usage

This is a non-exhaustive list of strictly forbidden uses of AI and LLM-based tools.

- Low-quality submissions that fail to satisfy our standard of code excellence.
- Submitting secrets, credentials, private user data, or unpublished security reports to third-party AI services.
  - This includes console keys, encryption material, and private vulnerability disclosures.

## Codebase Ingestion

Ingestion of this codebase by LLMs for any purpose--such as training or prompt context--is permitted *for now*. This may change at any point as litigation and court rulings around AI and code copyright proceed.

AI models may have ingested AGPLv3 or otherwise copyleft-incompatible code and may regurgitate it. You remain responsible for ensuring that all code you submit is compatible with the GPLv3 or later.

## Enforcement

- Maintainers reserve the right to rework or reject low-quality contributions.
- Repeated failures to comply with the requirements above may result in loss of contribution privileges.

## References

This policy document has been adapted from the AI policies of numerous other free and open-source projects, whether restrictive or permissive.

- [Gitea](https://github.com/go-gitea/gitea/blob/main/CONTRIBUTING.md#ai-contribution-policy)
- The [Linux kernel](https://kernel.org/doc/html/next/process/coding-assistants.html)
- [Gentoo Linux](https://wiki.gentoo.org/wiki/Project:Council/AI_policy)
- [cURL](https://curl.se/dev/contribute.html#on-ai-use-in-curl)
- [FastAPI](https://tiangolo.com/open-source/contributing/#automated-code-and-ai)
