# 049 - Configuration, documentation and launch script

Counterpart of [ReHLDS MT-701](https://github.com/aluzed/ReHLDS-MT/blob/master/docs/stories/MT-701-configuration-and-docs.md).

## Goal
Every option of stories 035-048 documented (`Documentation/dedicated-server-tuning.md`,
counterpart of ReHLDS' `docs/performance-tuning.md`): default, recommended
value, measured effect. One command to run the original behaviour for
comparisons. `scripts/server/run.sh` starts the server with every option on.

## Progress (2026-10-15)
- [x] `Documentation/dedicated-server-tuning.md`: every option of 035-044,
      default and recommended values, measured effects, counters, the machine.
- [x] `scripts/server/run.sh` starts with every option on, `FEATURES=0` with
      every option off.
- [ ] Again once stories 045-048 are done.

## Dependencies
All shipped stories.
