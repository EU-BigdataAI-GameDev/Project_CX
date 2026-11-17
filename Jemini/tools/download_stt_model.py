"""실시간 자막(POST /stt/partial)용 faster-whisper 모델을 미리 내려받는다(약 480MB, 한 번만).

    python tools/download_stt_model.py [--model small] [--dir models] [--test my.wav]

서버도 첫 실행 때 자동으로 받지만(STT_AUTO_DOWNLOAD=true), 패키징 전·오프라인 시연 전에는 미리 받아 둔다.
결과: <dir>/faster-whisper-<model>/ (model.bin, config.json, tokenizer.json, vocabulary.txt)
"""
from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from app import stt  # noqa: E402
from app.config import load_settings  # noqa: E402


def main() -> None:
    settings = load_settings()
    parser = argparse.ArgumentParser(description="Download the faster-whisper model for live captions")
    parser.add_argument("--model", default=settings.stt_model)
    parser.add_argument("--dir", default=str(settings.stt_model_dir))
    parser.add_argument("--test", help="WAV file to transcribe after loading (prints text and ms)")
    args = parser.parse_args()

    model_dir = Path(args.dir)
    if not model_dir.is_absolute():
        model_dir = ROOT / model_dir
    folder = stt.model_folder(model_dir, args.model)
    if stt.has_model(folder):
        print(f"[stt] already present: {folder}")
    else:
        print(f"[stt] downloading faster-whisper {args.model} -> {folder}")
        start = time.perf_counter()
        folder = stt.download(model_dir, args.model)
        print(f"[stt] done in {time.perf_counter() - start:.1f}s")
    size = sum(p.stat().st_size for p in folder.rglob("*") if p.is_file())
    print(f"[stt] size: {size / 1024 / 1024:.0f} MB")

    if args.test:
        from faster_whisper import WhisperModel

        start = time.perf_counter()
        model = WhisperModel(str(folder), device="cpu", compute_type="int8", cpu_threads=stt.default_cpu_threads())
        print(f"[stt] load: {(time.perf_counter() - start) * 1000:.0f} ms")
        fn = stt._make_transcriber(model)
        samples, seconds = stt.decode_wav(Path(args.test).read_bytes())
        start = time.perf_counter()
        text = fn(samples)
        print(f"[stt] {seconds:.2f}s audio -> {(time.perf_counter() - start) * 1000:.0f} ms: {text!r}")


if __name__ == "__main__":
    main()
