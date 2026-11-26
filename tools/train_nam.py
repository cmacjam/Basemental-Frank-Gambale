"""Train one Basemental FG 250 capture into a NAM A2 model (contains Full + Lite sizes).

Usage: python3 tools/train_nam.py <input.wav = NAM v3_0_0 sweep> <output.wav> <name> <tone_type> <epochs> <outdir>
Stop it with SIGINT (e.g. `timeout -s INT 300m ...`) and the best model so far is still exported.
"""
import json
import pathlib
import shutil
import sys
import time

from nam.models.metadata import GearType, ToneType, UserMetadata
from nam.train.core import train
from nam.train.metadata import TRAINING_KEY

inp, out, name, tone, epochs, outdir = sys.argv[1:7]
outdir = pathlib.Path(outdir)
work = pathlib.Path("nam_work") / pathlib.Path(out).stem
work.mkdir(parents=True, exist_ok=True)

meta = UserMetadata(
    name=name,
    modeled_by="Basemental",
    gear_type=GearType.AMP,
    gear_make="Basemental",
    gear_model="FG 250 (plug-in capture)",
    tone_type=ToneType(tone),
)
t0 = time.time()
res = train(
    input_path=inp,
    output_path=out,
    train_path=str(work),
    epochs=int(epochs),
    latency=0,              # digital render: no round-trip latency
    seed=0,
    save_plot=True,
    silent=True,
    modelname=pathlib.Path(out).stem,
    local=False,
    user_metadata=meta,
)
if res is None or res.model is None:
    print("training failed its data checks:", res.metadata.model_dump_json(indent=1) if res else "")
    sys.exit(1)

exp = work / "export"
exp.mkdir(exist_ok=True)
res.model.net.export(exp, basename=pathlib.Path(out).stem, user_metadata=meta,
                     other_metadata={TRAINING_KEY: res.metadata.model_dump()})
outdir.mkdir(parents=True, exist_ok=True)
for f in exp.glob("*.nam"):
    shutil.copy(f, outdir / f.name)
png = work / (pathlib.Path(out).stem + ".png")
if png.exists():
    shutil.copy(png, outdir / png.name)
esr = res.metadata.validation_esr
summary = {"name": name, "file": pathlib.Path(out).stem + ".nam", "validation_esr": esr,
           "minutes": round((time.time() - t0) / 60, 1)}
(outdir / (pathlib.Path(out).stem + ".json")).write_text(json.dumps(summary, indent=1))
print(json.dumps(summary))
