# On AI-assisted development

<sub>[← Back to the README](../README.md)</sub>

**Yes, AI assistance is used here, heavily.** What people rightly call "AI slop" is *unverified output
pushed onto other people* — and none of that happens here: **no patches from this fork are pushed at the
KeeperFX team** (the weekly sync runs inbound only), and every line of the [delta](whats-different.md) is compiled and
**played on real Linux hardware** before it ships. A mistake here costs me a debugging session, never
someone else's review queue. **Judge the diff, not the toolchain** — it's all right there, one commit at a time.

## The longer answer — what the strictest C projects actually do, with sources

The slop problem is real: curl [ended its six-year bug bounty](https://www.theregister.com/2026/01/21/curl_ends_bug_bounty/)
in January 2026 as valid reports collapsed, and the ["AI-DDoS"](https://arxiv.org/abs/2607.04003) on
maintainers is now a measured phenomenon — submissions that cost minutes to make and hours to disprove.

**Yet the most conservative C projects alive permit AI-assisted code, provided a human is accountable:**

| Project | Position |
|---|---|
| **Linux kernel** | [Permitted](https://docs.kernel.org/process/coding-assistants.html), tagged `Assisted-by:`. AI agents **must not** add `Signed-off-by` — only a human certifies the DCO and takes "full responsibility". |
| **LLVM** | [Permitted](https://llvm.org/docs/AIToolPolicy.html), human in the loop: "The contributor is always the author and is fully accountable." |
| **curl** | Ended its bounty over slop, yet still: "[We can accept code written with the help of AI](https://curl.se/dev/contribute.html), but the code must still follow coding standards…" |

Across 118 repos with a written AI policy, [78% permit it](https://arxiv.org/abs/2605.16706) and 74%
require a human in the loop. The rule is **accountability, not abstinence**.

**At scale it's measured, not hypothetical.** Google's year-long study: 595 changes,
[74.45% LLM-generated, ~50% less migration time](https://arxiv.org/abs/2504.09691); about
[75% of Google's new code](https://www.techspot.com/news/112152-google-ai-now-generates-75-new-code-up.html)
is now AI-generated and engineer-reviewed. Amazon upgraded
[30,000+ Java apps](https://aws.amazon.com/blogs/devops/amazon-q-developer-just-reached-a-260-million-dollar-milestone)
(~4,500 developer-years). [90% of developers](https://dora.dev/dora-report-2025/) use AI (DORA 2025).

**And the honest other half:** that same DORA report ties AI adoption to *higher* instability and calls it
an **amplifier** of whatever practice already exists, good or bad — while a
[METR trial](https://metr.org/blog/2025-07-10-early-2025-ai-experienced-os-dev-study/) found experienced
developers 19% *slower* with AI while feeling 20% faster. Both point the same way: the tool isn't the
variable that matters, the verification gate is. Hence the gate here — it compiles, it runs, I played it,
and I own the bug report. Found something sloppy or broken?
[Open an issue](https://github.com/ForkedInTime/keeperfx-tux-edition/issues).
