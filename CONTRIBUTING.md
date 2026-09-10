# Contributing a smell session

## What makes a session useful

A session is a continuous stretch of scans with one label. Useful sessions have:

- **The boring class too.** "Normal hive air" needs as many sessions as the
  interesting states, or a classifier has nothing to contrast against.
- **Repetition.** One presentation of a smell teaches nothing. Ten, spread over
  different days and temperatures, teach a shape.
- **Honest labels.** If you are not sure the colony was robbing, say so in `notes`
  and set `confidence` to `"unsure"`. A wrong label is worse than no label.
- **Context.** Temperature and humidity change a MOX sensor's response, which is why
  they travel with every scan.

## How to submit

1. Export a session (see [DATA_FORMAT.md](DATA_FORMAT.md); the Dusk Apiary dashboard
   has an **Export for sharing** button in the Nose panel).
2. Validate it:
   ```
   python tools/validate.py my-session.json
   ```
3. Put it in `data/<your-apiary-id>/sessions/` and add `apiary.json` if this is your
   first contribution.
4. Open a pull request. CI re-runs the validator on every file in the PR.

No git? Open a **Smell submission** issue and attach the JSON; a maintainer will file
it for you.

## Apiary ids

Pick something short, lower-case and stable: `hive-observatory-pa`, `bienenhof-nrw`.
It only needs to be unique in this repository.

## Privacy

`apiary.json` asks for a coarse location — region and climate zone, not an address.
Please keep it coarse. Nothing in a session file should identify a person.
