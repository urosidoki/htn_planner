# Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com
"""Rebuild, package, relocate and validate the generated-only Linux SDK."""
import argparse
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import uuid

from ValidatePackageLinux import VARIANTS, FORBIDDEN_PATH, digest, require, run


def replace_package(stage, staged_archive, destination, archive):
    """Replace only this version's local outputs after archive validation succeeds."""
    checksum = Path(str(archive) + ".sha256")
    dist = destination.parent.resolve()
    for output in (destination, archive, checksum):
        require(not output.is_symlink() and output.resolve().parent == dist,
                f"Package output must be a direct path inside dist: {output}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists():
        shutil.rmtree(destination)
    shutil.move(stage, destination)
    os.replace(staged_archive, archive)
    checksum.write_text(f"{digest(archive)}  {archive.name}\n")


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", default=(root / "VERSION").read_text().strip())
    parser.add_argument("--jobs", type=int, default=int(os.environ.get("HTN_BUILD_JOBS", "4")))
    args = parser.parse_args()
    require(platform.system() == "Linux" and platform.machine() == "x86_64", "Run this script in Linux x86_64 (WSL2 is supported)")
    require(re.fullmatch(r"\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?", args.version), "Invalid SDK version")
    require(args.jobs > 0, "--jobs must be positive")
    cc, cxx = os.environ.get("CC", "gcc-14"), os.environ.get("CXX", "g++-14")
    premake = os.environ.get("PREMAKE5", "premake5")
    for tool in (cc, cxx, premake, "make", "cmake", "ninja", "nm", "readelf", "gcc-14", "g++-14", "clang-18", "clang++-18"):
        require(shutil.which(tool), f"Missing tool: {tool}; see docs/LINUX.md")
    name = f"HTNSDK-{args.version}-linux-x86_64"
    destination, archive = root / "dist" / name, root / "dist" / f"{name}.tar.gz"
    build_id = uuid.uuid4().hex
    work = root / "build" / "sdk-linux" / build_id
    work.mkdir(parents=True)
    compiler_version = subprocess.check_output([cxx, "--version"], text=True).splitlines()[0]
    toolset = "clang" if "clang" in compiler_version.lower() else "gcc"
    run([premake, "--sdk", f"--cc={toolset}", "gmake"], work / "premake.log", cwd=root)
    # Inspect generated source rules; a recursive make dry-run cannot resolve
    # cross-project library dependencies until those files have been built.
    sources = set()
    for component in ("HTNFramework", "HTNIntegration", "HTNRuntimeBridge", "HTNTranslator"):
        makefile = (root / "build/sdk" / component / "Makefile").read_text()
        sources.update(re.findall(r"[^\s\"']+\.cpp", makefile))
    require(len(sources) > 20 and not any(FORBIDDEN_PATH.search(p) for p in sources), "SDK source boundary failed")
    (work / "source-boundary.log").write_text("PASS: isolated SDK compilation sources\n" + "\n".join(sorted(sources)) + "\n")
    variants = []
    abi_probe = work / "abi.cpp"
    abi_probe.write_text('#include "Translator/HTNGeneratedPlanner.h"\n#include "Translator/HTNRuntimeBridge.h"\n'
                         '#include <cstdio>\n#include <string>\n'
                         '#if !defined(__GLIBCXX__) || _GLIBCXX_USE_CXX11_ABI != 1 || defined(_GLIBCXX_DEBUG)\n#error Unsupported libstdc++ ABI\n#endif\n'
                         'int main() { std::printf("%u %u\\n", HTN_GENERATED_PLANNER_ABI_VERSION, HTN_RUNTIME_BRIDGE_ABI_VERSION); }\n')
    for variant in VARIANTS:
        debug, instrumented = variant.startswith("Debug"), variant.endswith("Instrumented")
        defines = ["HTN_DEBUG" if debug else "HTN_RELEASE", "_DEBUG" if debug else "NDEBUG"]
        if instrumented:
            defines += ["HTN_ENABLE_LOGGING", "HTN_VALIDATE_DOMAIN", "HTN_DEBUG_DECOMPOSITION"]
        # Remove archives too: ar can retain obsolete members after a source is removed.
        run(["make", "-C", "build/sdk", f"config={variant.lower()}", "clean"], work / f"{variant}-clean.log", cwd=root)
        run(["make", "-C", "build/sdk", "-B", f"-j{args.jobs}", f"config={variant.lower()}", f"CC={cc}", f"CXX={cxx}"],
            work / f"{variant}-build.log", cwd=root)
        run([cxx, "-std=c++20", *[f"-D{d}" for d in defines], "-I", root / "HTNFramework/src", abi_probe, "-o", work / "abi"],
            work / f"{variant}-abi.log")
        planner_abi, bridge_abi = map(int, subprocess.check_output([work / "abi"], text=True).split())
        library = root / f"bin/sdk/{variant}-linux-x86_64/HTNFramework/libHTNFramework.a"
        symbols = subprocess.check_output(["nm", "-C", "--defined-only", library], text=True, stderr=subprocess.DEVNULL)
        require(not re.search(r"HTNDomainInterpreter|HTNNodeVisitor|HTNInterpretedPlanner|HTNInterpretedPlanningUnit", symbols),
                f"Interpreter symbols in {variant}")
        variants.append(dict(id=variant, runtime_configuration="Debug" if debug else "Release", instrumentation=instrumented,
                             optimization="off" if debug else "full", symbols=True, defines=defines,
                             generated_planner_abi=f"0x{planner_abi:08X}", runtime_bridge_abi=f"0x{bridge_abi:08X}"))
    stage = work / name
    stage.mkdir()
    artifacts = []

    def copy(source, relative, binary=False):
        target = stage / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        if binary:
            value = digest(source)
            require(digest(target) == value, f"Artifact changed while packaging: {source}")
            artifacts.append(dict(path=source.relative_to(root).as_posix(), package_path=relative, sha256=value))

    for component in ("HTNFramework", "HTNIntegration"):
        for header in (root / component / "src").rglob("*.h"):
            relative = header.relative_to(root / component / "src").as_posix()
            if header.name != "pch.h" and not FORBIDDEN_PATH.search(relative):
                copy(header, f"include/{component}/{relative}")
    for variant in VARIANTS:
        for component in ("HTNFramework", "HTNIntegration", "HTNRuntimeBridge"):
            filename = f"lib{component}.{'so' if component == 'HTNRuntimeBridge' else 'a'}"
            copy(root / f"bin/sdk/{variant}-linux-x86_64/{component}/{filename}", f"lib/linux-x86_64/{variant}/{filename}", True)
    copy(root / "bin/sdk/ReleasePlain-linux-x86_64/HTNTranslator/HTNTranslator", "bin/linux-x86_64/tools/HTNTranslator", True)
    copy(root / "SDK/HTNConfigLinux.cmake", "cmake/HTNConfig.cmake")
    copy(root / "SDK/ValidatePackageLinux.py", "ValidatePackage.py")
    copy(root / "SDK/ValidatePackageLinux.sh", "ValidatePackage.sh")
    copy(root / "SDK/PackageREADMELinux.md", "README.md")
    copy(root / "LICENSE", "LICENSE")
    copy(root / "NOTICE.md", "NOTICE.md")
    copy(root / "ThirdParty/optick/LICENSE", "THIRD_PARTY_NOTICES/Optick-LICENSE.txt")
    for filename in ("LINUX.md", "DOMAIN_LANGUAGE.md", "TYPE_CONVERSION.md", "GENERATED_RECURSION.md", "ASSIGNMENT.md",
                     "AXIOM_OVERLOADS.md", "METHOD_OVERLOADS.md", "MISSING_CALLTERMS.md", "RUNTIME_LISTS.md",
                     "GENERATED_INSTRUMENTATION.md", "USE_CASES.md", "SDK_VARIANTS.md", "RELEASE_2_4_0.md", "RELEASE_2_3_0.md", "RELEASE_NOTES_NEGATIVE_LITERALS.md",
                     "RELEASE_2_2_0.md", "RELEASE_2_1_0.md", "RELEASE_2_0_4.md", "RELEASE_2_0_3.md",
                     "AAA_COMBAT_NPC_DEMO.md", "COMPILER_IR.md", "GENERATED_DEBUGGER.md",
                     "LINUX_SMOKE.md", "RELEASE_2_0_0.md", "RELEASE_NOTES_AXIOM_ASSIGNMENTS.md",
                     "RELEASE_NOTES_BOOLEAN_CALLTERMS.md", "RELEASE_NOTES_GENERATED_DEBUGGER.md", "RELEASE_NOTES_NESTED_CALLS.md",
                     "RELEASE_NOTES_WRITE_FACT.md", "INSTANCE_LIST_ALLOCATOR.md",
                     "BACKTRACKING_ALLOCATOR.md", "RELEASE_NOTES_INSTANCE_ALLOCATOR.md"):
        copy(root / "docs" / filename, f"docs/{filename}")
    for source in (root / "SDK/Examples").rglob("*"):
        if source.is_file() and source.suffix in (".cpp", ".domain", ".txt", ".cmake", ".py"):
            copy(source, "examples/" + source.relative_to(root / "SDK/Examples").as_posix())
    for source in (root / "HTNFramework").rglob("*.natvis"):
        copy(source, "debug/" + source.name)
    receipt = dict(version=args.version, build_id=build_id, rebuilt=True, artifacts=artifacts)
    (stage / "build-provenance.json").write_text(json.dumps(receipt, indent=2) + "\n")
    manifest = dict(schema_version=2, sdk_version=args.version, platform="linux-x86_64", architecture="x86_64",
                    compiler=compiler_version, c_standard="C11", cpp_standard="C++20", standard_library="libstdc++",
                    glibc=platform.libc_ver()[1], libstdcxx_cxx11_abi=1, libstdcxx_debug=False,
                    build_id=build_id, variants=variants,
                    components=[dict(name=n, kind=k) for n, k in (("HTNFramework", "static-library"),
                        ("HTNIntegration", "static-library"), ("HTNTranslator", "executable"), ("HTNRuntimeBridge", "dynamic-bridge"))])
    (stage / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    for path in (stage / "bin/linux-x86_64/tools/HTNTranslator", stage / "ValidatePackage.sh"):
        path.chmod(0o755)
    (stage / "CHECKSUMS.sha256").write_text("".join(f"{digest(p)}  {p.relative_to(stage).as_posix()}\n"
        for p in sorted(stage.rglob("*")) if p.is_file()))
    # Validate only an extracted, relocated archive. Publish to dist only after both compilers pass.
    staged_archive = work / archive.name
    with tarfile.open(staged_archive, "w:gz") as output:
        output.add(stage, arcname=name)
    external = Path(tempfile.mkdtemp(prefix="htn-sdk-linux-extracted-"))
    with tarfile.open(staged_archive) as source:
        source.extractall(external, filter="data")
    extracted = external / name
    for consumer_cc, consumer_cxx in (("gcc-14", "g++-14"), ("clang-18", "clang++-18")):
        consumer_build = external / consumer_cc
        try:
            run([sys.executable, extracted / "ValidatePackage.py", "--build-root", consumer_build,
                 "--cc", consumer_cc, "--cxx", consumer_cxx, "--jobs", args.jobs], work / f"consumers-{consumer_cc}.log")
        finally:
            # /tmp may be cleared when WSL stops. Preserve detailed evidence with the build.
            logs = work / "external" / consumer_cc
            logs.mkdir(parents=True, exist_ok=True)
            for log in consumer_build.glob("*.log"):
                shutil.copy2(log, logs / log.name)
    replace_package(stage, staged_archive, destination, archive)
    print(f"PASS: Linux SDK archive and SHA-256: {archive}\nBuild logs: {work}\nExternal consumers and logs: {external}", flush=True)


if __name__ == "__main__":
    main()
