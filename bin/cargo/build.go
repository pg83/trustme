package main

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sort"
	"strconv"
	"strings"
)

const (
	trustmeCargoDumpCommand          = "TRUSTME_CARGO_DUMP_COMMAND"
	trustmeCargoDumpEnv              = "TRUSTME_CARGO_DUMP_ENV"
	trustmeCargoDylib                = "TRUSTME_CARGO_DYLIB"
	trustmeCargoIgnoreToolTimestamps = "TRUSTME_CARGO_IGNORE_TOOL_TIMESTAMPS"
	trustmeCargoNoDebugAssertions    = "TRUSTME_CARGO_NO_DEBUG_ASSERTIONS"
	trustmeCargoVendorDir            = "TRUSTME_CARGO_VENDOR_DIR"
	trustmeCargoLibSearch            = "TRUSTME_CARGO_LIB_SEARCH"
)

type Builder struct {
	context            *BuildContext
	tasks              map[string]*Task
	units              map[*Task]*CompileUnit
	systemHostLoaded   bool
	systemTargetLoaded bool
	systemHost         []ExternalCrateArtifact
	systemTarget       []ExternalCrateArtifact
}

type ExternalCrateArtifact struct {
	alias    string
	name     string
	metadata string
	object   string
}

type CompileUnit struct {
	pkg          *Package
	target       *Target
	isHost       bool
	baseName     string
	rs           *Task
	metadata     int
	cpp          int
	linkManifest int
	cc           *Task
	final        *Task
}

type InstallArtifact struct {
	task   *Task
	index  int
	path   string
	binary bool
}

func buildProject(opts BuildOptions) []string {
	manifestPath := opts.manifestPath

	if manifestPath == "" {
		manifestPath = "Cargo.toml"
	}

	manifestPath = absolutePath(manifestPath)

	workspace := findWorkspace(manifestPath)
	doc := readToml(manifestPath)
	members := workspaceMemberManifests(workspace)

	if opts.packageName != "" {
		manifestPath = selectWorkspacePackage(workspace, members, opts.packageName)
		opts.manifestPath = manifestPath
	} else if opts.workspaceAll || mapValue(doc["package"]) == nil && len(members) > 0 {
		selected := members

		if !opts.workspaceAll && len(workspace.defaultMembers) > 0 {
			selected = workspacePatterns(workspace, workspace.defaultMembers)
		}

		var binaries []string

		for _, member := range selected {
			name := stringValue(mapValue(readToml(member)["package"])["name"])

			if contains(opts.excludePackages, name) {
				continue
			}

			memberOpts := opts

			memberOpts.manifestPath = member
			memberOpts.workspaceAll = false
			memberOpts.packageName = ""

			binaries = append(binaries, buildProject(memberOpts)...)
		}

		return binaries
	}

	return buildPackage(opts, manifestPath)
}

func buildPackage(opts BuildOptions, manifestPath string) []string {
	workspace := findWorkspace(manifestPath)
	repository := newRepository(workspace, opts.vendorDir)
	root := repository.loadPath(manifestPath)
	compiler := os.Getenv("TRUSTME_PATH")

	if compiler == "" {
		compiler = siblingCompiler()
	}

	host := hostTriple()
	target := opts.target

	if target == "" {
		target = host
	}

	context := &BuildContext{
		opts:       opts,
		repository: repository,
		root:       root,
		workspace:  workspace,
		compiler:   compiler,
		host:       host,
		target:     target,
		cross:      opts.target != "" && opts.target != host && !opts.emitMmir,
	}

	context.cfg = compilerCfg(compiler, opts.target)
	context.cxx = compilerCxxSpec(compiler, opts.target)
	context.hostCxx = context.cxx

	if context.cross {
		context.hostCxx = compilerCxxSpec(compiler, "")
	}

	resolveGraph(context)

	builder := &Builder{context: context, tasks: map[string]*Task{}, units: map[*Task]*CompileUnit{}}
	roots, artifacts := builder.rootTasks()

	if opts.publishDeps {
		publishedRoots, publishedArtifacts := builder.publishedDependencies()
		roots = append(roots, publishedRoots...)
		artifacts = append(artifacts, publishedArtifacts...)
	}

	executor := runTasks(roots, opts.jobs, builder.cacheRoot(), opts.dryRun)
	var binaries []string

	if !opts.dryRun {
		for _, artifact := range artifacts {
			executor.install(artifact.task, artifact.index, artifact.path)

			if artifact.binary {
				binaries = append(binaries, artifact.path)
			}
		}
	}

	if opts.command == "test" && !opts.noRun {
		for _, binary := range binaries {
			builder.runTest(binary, opts.testArgs)
		}
	}

	return binaries
}

func (b *Builder) publishedDependencies() ([]*Task, []InstallArtifact) {
	units := make([]*CompileUnit, 0, len(b.units))

	for task, unit := range b.units {
		if task == unit.rs && unit.target.kind == "lib" {
			units = append(units, unit)
		}
	}

	sort.Slice(units, func(i, j int) bool {
		return b.artifact(units[i].pkg, units[i].target, units[i].isHost) <
			b.artifact(units[j].pkg, units[j].target, units[j].isHost)
	})

	var roots []*Task
	var artifacts []InstallArtifact

	for _, unit := range units {
		metadataPath := b.artifact(unit.pkg, unit.target, unit.isHost)

		if unit.target.procMacro || crateType(unit.target) == "dylib" {
			metadataPath += ".rlib"
		}

		if unit.metadata >= 0 {
			artifacts = append(artifacts, InstallArtifact{
				task: unit.rs, index: unit.metadata, path: metadataPath,
			})

			// HIR keeps this conventional unique filename as the fallback for
			// standalone -L loading.  Some published artifacts (notably core,
			// alloc, and proc macros) have a deliberately shorter public name,
			// so publish a second hardlink without duplicating CAS contents.
			compatibilityPath := filepath.Join(
				filepath.Dir(metadataPath), "lib"+b.crateName(unit)+".rlib")
			if compatibilityPath != metadataPath {
				artifacts = append(artifacts, InstallArtifact{
					task: unit.rs, index: unit.metadata, path: compatibilityPath,
				})
			}
		}

		if unit.target.procMacro || crateType(unit.target) == "dylib" {
			final := b.finalTask(unit.rs)
			roots = append(roots, final)
			artifacts = append(artifacts, InstallArtifact{
				task: final, index: 0,
				path: b.artifact(unit.pkg, unit.target, true),
			})
		} else {
			object := b.codegenTask(unit.rs)
			roots = append(roots, object)
			artifacts = append(artifacts, InstallArtifact{
				task: object, index: 0, path: metadataPath + ".o",
			})

			compatibilityPath := filepath.Join(
				filepath.Dir(metadataPath), "lib"+b.crateName(unit)+".rlib.o")
			if compatibilityPath != metadataPath+".o" {
				artifacts = append(artifacts, InstallArtifact{
					task: object, index: 0, path: compatibilityPath,
				})
			}
		}
	}

	return roots, artifacts
}

func workspaceMemberManifests(workspace *Workspace) []string {
	return workspacePatterns(workspace, workspace.members)
}

func workspacePatterns(workspace *Workspace, patterns []string) []string {
	seen := map[string]bool{}

	var result []string

	for _, pattern := range patterns {
		matches := throw2(filepath.Glob(filepath.Join(workspace.dir, pattern)))

		for _, match := range matches {
			manifest := match

			if filepath.Base(manifest) != "Cargo.toml" {
				manifest = filepath.Join(manifest, "Cargo.toml")
			}

			manifest = absolutePath(manifest)

			if fileExists(manifest) && !seen[manifest] && !workspacePathExcluded(workspace, manifest) {
				seen[manifest] = true
				result = append(result, manifest)
			}
		}
	}

	sort.Strings(result)

	return result
}

func workspacePathExcluded(workspace *Workspace, manifest string) bool {
	dir := filepath.Dir(manifest)

	for _, pattern := range workspace.exclude {
		matches := throw2(filepath.Glob(filepath.Join(workspace.dir, pattern)))

		for _, match := range matches {
			if absolutePath(match) == dir {
				return true
			}
		}
	}

	return false
}

func selectWorkspacePackage(workspace *Workspace, members []string, want string) string {
	root := workspace.manifestPath

	if mapValue(readToml(root)["package"]) != nil {
		members = append(members, root)
	}

	for _, manifest := range members {
		pkg := mapValue(readToml(manifest)["package"])

		if stringValue(pkg["name"]) == want {
			return manifest
		}
	}

	throwFmt("package %q is not a workspace member", want)

	return ""
}

func (b *Builder) rootTasks() ([]*Task, []InstallArtifact) {
	root := b.context.root
	isHost := !b.context.cross

	var tasks []*Task
	var artifacts []InstallArtifact

	selectors := b.context.opts.selectors

	explicit := selectors.lib || selectors.bins || len(selectors.bin) > 0 || selectors.tests ||
		len(selectors.test) > 0 || selectors.examples || len(selectors.example) > 0 ||
		selectors.benches || len(selectors.bench) > 0

	if b.context.opts.command == "build" || b.context.opts.command == "check" {
		checkOnly := b.context.opts.command == "check"
		if !explicit || selectors.lib {
			if task := b.libraryTask(root, isHost); task != nil {
				rootTask := task
				if !checkOnly {
					rootTask = b.finalTask(task)
				}
				tasks = append(tasks, rootTask)
				unit := b.units[task]

				if !checkOnly && unit.metadata >= 0 {
					path := b.artifact(root, unit.target, isHost)

					if unit.target.procMacro || crateType(unit.target) == "dylib" {
						path += ".rlib"
					}

					artifacts = append(artifacts, InstallArtifact{
						task: task, index: unit.metadata,
						path: path,
					})
				}

				if !checkOnly && (unit.target.procMacro || crateType(unit.target) == "dylib") {
					artifacts = append(artifacts, InstallArtifact{
						task: b.finalTask(task), index: 0,
						path: b.artifact(root, unit.target, true),
					})
				} else if !checkOnly && crateType(unit.target) == "rlib" {
					artifacts = append(artifacts, InstallArtifact{
						task: b.codegenTask(task), index: 0,
						path: b.artifact(root, unit.target, isHost) + ".o",
					})
				}
			}
		}

		for _, target := range root.targets {
			selected := !explicit && target.kind == "bin" ||
				target.kind == "bin" && (selectors.bins || contains(selectors.bin, target.name)) ||
				target.kind == "example" && (selectors.examples || contains(selectors.example, target.name)) ||
				target.kind == "bench" && (selectors.benches || contains(selectors.bench, target.name)) ||
				target.kind == "test" && (selectors.tests || contains(selectors.test, target.name))

			if selected && targetFeaturesEnabled(root, target) {
				task := b.targetTask(root, target, isHost)
				rootTask := task
				if !checkOnly {
					rootTask = b.finalTask(task)
				}
				tasks = append(tasks, rootTask)

				if !checkOnly && target.kind != "lib" {
					artifacts = append(artifacts, InstallArtifact{
						task: rootTask, index: 0, path: b.artifact(root, target, isHost), binary: true,
					})
				}
			}
		}
	} else {
		lib := packageLibrary(root)

		if lib != nil && (!explicit || selectors.lib) && lib.test {
			target := *lib

			target.kind = "test"
			target.name += "-test"

			task := b.targetTask(root, &target, isHost)

			final := b.finalTask(task)
			tasks = append(tasks, final)
			artifacts = append(artifacts, InstallArtifact{
				task: final, index: 0, path: b.artifact(root, &target, isHost), binary: true,
			})
		}

		for _, target := range root.targets {
			selected := target.kind == "test" && target.test && (!explicit || selectors.tests || contains(selectors.test, target.name)) ||
				target.kind == "bin" && target.test && !explicit ||
				target.kind == "bin" && selectors.bins ||
				target.kind == "bin" && contains(selectors.bin, target.name) ||
				target.kind == "example" && (selectors.examples || contains(selectors.example, target.name)) ||
				target.kind == "bench" && (selectors.benches || contains(selectors.bench, target.name))

			if selected && targetFeaturesEnabled(root, target) {
				copy := *target

				copy.kind = "test"

				task := b.targetTask(root, &copy, isHost)

				final := b.finalTask(task)
				tasks = append(tasks, final)
				artifacts = append(artifacts, InstallArtifact{
					task: final, index: 0, path: b.artifact(root, &copy, isHost), binary: true,
				})
			}
		}
	}

	if len(tasks) == 0 {
		throwFmt("package %s has no selected targets", root.name)
	}

	return tasks, artifacts
}

func (b *Builder) libraryTask(pkg *Package, isHost bool) *Task {
	if pkg.magic {
		return nil
	}

	target := packageLibrary(pkg)

	if target == nil {
		return nil
	}

	return b.targetTask(pkg, target, isHost || target.procMacro)
}

func (b *Builder) targetTask(pkg *Package, target *Target, isHost bool) *Task {
	key := b.unitKey("rs", pkg, target, isHost)

	if task := b.tasks[key]; task != nil {
		return task
	}

	baseName := b.artifactName(pkg, target)
	outputs := []TaskOutput{}
	metadata := -1

	if target.kind == "lib" {
		metadata = len(outputs)
		name := baseName

		if target.procMacro || crateType(target) == "dylib" {
			name += ".rlib"
		}

		outputs = append(outputs, TaskOutput{name: name})
	}

	cpp := len(outputs)
	outputs = append(outputs, TaskOutput{name: baseName + ".cpp"})
	linkManifest := len(outputs)
	outputs = append(outputs, TaskOutput{name: baseName + ".link"})
	task := &Task{
		key:       key,
		name:      b.taskName(pkg, target, isHost),
		kind:      "RS",
		inputs:    b.targetInputs(pkg, target),
		outputs:   outputs,
		signature: b.rustSignature(pkg, target, isHost),
	}

	if !ignoreToolTimestamps() {
		task.inputs = append(task.inputs, b.context.compiler)
	}
	b.addSystemInputs(task, isHost, false)

	b.tasks[key] = task
	unit := &CompileUnit{
		pkg: pkg, target: target, isHost: isHost, baseName: baseName, rs: task,
		metadata: metadata, cpp: cpp, linkManifest: linkManifest,
	}
	b.units[task] = unit

	if target.kind != "lib" {
		if libTask := b.libraryTask(pkg, isHost); libTask != nil {
			if lib := packageLibrary(pkg); lib != nil && lib.procMacro {
				task.deps = append(task.deps, b.finalTask(libTask))
			} else {
				task.deps = append(task.deps, libTask)
			}
		}
	}

	for _, dep := range b.compileDependencies(pkg, target.kind == "test") {
		if dep.packageRef == nil {
			throwFmt("internal: unresolved dependency %s of %s", dep.key, pkg.name)
		}

		depHost := isHost

		if lib := packageLibrary(dep.packageRef); lib != nil && lib.procMacro {
			depHost = true
		}

		if depTask := b.libraryTask(dep.packageRef, depHost); depTask != nil {
			if lib := packageLibrary(dep.packageRef); lib != nil && lib.procMacro {
				task.deps = append(task.deps, b.finalTask(depTask))
			} else {
				task.deps = append(task.deps, depTask)
			}
		}
	}

	if script := b.buildScriptRunTask(pkg); script != nil {
		task.deps = append(task.deps, script)
	}

	task.action = func(ctx *TaskContext) {
		outDir := ""

		if script := b.buildScriptRunTask(pkg); script != nil {
			outDir = ctx.tree(script, 1)
		}

		b.compileTarget(ctx, unit, outDir)
	}

	return task
}

func (b *Builder) buildScriptCompileTask(pkg *Package) *Task {
	if pkg.buildScript == "" {
		return nil
	}

	key := "rs|build-script|" + pkg.manifestPath + "|" + b.context.opts.profile

	if task := b.tasks[key]; task != nil {
		return task
	}

	target := &Target{
		kind: "build-script", name: "build", path: pkg.buildScript, edition: pkg.edition,
	}
	baseName := b.buildScriptBase(pkg) + "_run"
	task := &Task{
		key:       key,
		name:      pkg.name + " v" + pkg.version.string() + " (build script)",
		kind:      "RS",
		inputs:    b.packageInputs(pkg),
		outputs:   []TaskOutput{{name: baseName + ".cpp"}, {name: baseName + ".link"}},
		signature: b.rustSignature(pkg, target, true),
	}

	if !ignoreToolTimestamps() {
		task.inputs = append(task.inputs, b.context.compiler)
	}
	b.addSystemInputs(task, true, false)

	b.tasks[key] = task
	unit := &CompileUnit{
		pkg: pkg, target: target, isHost: true, baseName: baseName, rs: task,
		metadata: -1, cpp: 0, linkManifest: 1,
	}
	b.units[task] = unit

	for _, dep := range b.buildDependencies(pkg) {
		if depTask := b.libraryTask(dep.packageRef, true); depTask != nil {
			if lib := packageLibrary(dep.packageRef); lib != nil && lib.procMacro {
				task.deps = append(task.deps, b.finalTask(depTask))
			} else {
				task.deps = append(task.deps, depTask)
			}
		}
	}

	task.action = func(ctx *TaskContext) {
		b.compileBuildScript(ctx, unit)
	}

	return task
}

func (b *Builder) buildScriptRunTask(pkg *Package) *Task {
	if pkg.buildScript == "" {
		return nil
	}

	key := "build-script-run|" + pkg.manifestPath + "|" + b.context.opts.profile

	if task := b.tasks[key]; task != nil {
		return task
	}

	task := &Task{
		key:       key,
		name:      pkg.name + " v" + pkg.version.string() + " (build script run)",
		kind:      "RUN",
		inputs:    b.packageInputs(pkg),
		outputs:   []TaskOutput{{name: b.buildScriptBase(pkg) + ".txt"}, {name: b.buildScriptBase(pkg) + ".out", tree: true}},
		signature: b.buildScriptRunSignature(pkg),
	}

	b.tasks[key] = task
	compile := b.buildScriptCompileTask(pkg)
	task.deps = append(task.deps, b.finalTask(compile))

	for _, dep := range b.mainDependencies(pkg, false) {
		if depTask := b.libraryTask(dep.packageRef, !b.context.cross); depTask != nil {
			task.deps = append(task.deps, depTask)
		}
	}

	task.action = func(ctx *TaskContext) {
		b.runBuildScript(ctx, pkg, b.finalTask(compile))
	}
	task.after = func(ctx *TaskContext) {
		if !b.context.opts.dryRun {
			loadBuildScriptOutput(pkg, ctx.file(task, 0))
		}
	}

	return task
}

func (b *Builder) finalTask(compile *Task) *Task {
	if compile == nil {
		return compile
	}

	unit := b.units[compile]

	if unit == nil {
		throwFmt("internal: no compile unit for %s", compile.name)
	}

	if unit.final != nil {
		return unit.final
	}

	cc := b.codegenTask(compile)
	needsLink := unit.target.kind != "lib" || unit.target.procMacro || crateType(unit.target) != "rlib"

	if !needsLink {
		unit.final = cc

		return cc
	}

	key := strings.Replace(unit.rs.key, "rs|", "ld|", 1)
	output := unit.baseName
	cxx := b.cxxSpec(unit.isHost)
	task := &Task{
		key:       key,
		name:      compile.name + " (link)",
		kind:      "LD",
		deps:      []*Task{cc},
		inputs:    []string{cxx.compiler},
		outputs:   []TaskOutput{{name: output, executable: unit.target.kind != "lib" || unit.target.procMacro}},
		signature: append([]string{"link"}, b.cxxSignature(unit.isHost)...),
	}
	b.addSystemInputs(task, unit.isHost, true)
	b.tasks[key] = task
	unit.final = task
	b.units[task] = unit
	linkedUnits := b.linkedUnits(unit)

	for _, linked := range linkedUnits {
		task.deps = append(task.deps, b.codegenTask(linked.rs))
	}

	task.action = func(ctx *TaskContext) {
		b.linkUnit(ctx, unit, linkedUnits)
	}

	return task
}

func (b *Builder) codegenTask(compile *Task) *Task {
	unit := b.units[compile]

	if unit.cc != nil {
		return unit.cc
	}

	key := strings.Replace(unit.rs.key, "rs|", "cc|", 1)
	object := unit.baseName + ".o"
	cxx := b.cxxSpec(unit.isHost)
	task := &Task{
		key:       key,
		name:      compile.name + " (C++)",
		kind:      "CC",
		deps:      []*Task{compile},
		inputs:    []string{cxx.compiler},
		outputs:   []TaskOutput{{name: object}},
		signature: append([]string{"compile"}, b.cxxSignature(unit.isHost)...),
	}
	b.tasks[key] = task
	unit.cc = task
	b.units[task] = unit
	task.action = func(ctx *TaskContext) {
		args := b.cxxCompileArgs(unit.isHost)
		// CAS paths intentionally have no semantic filename. Tell the compiler
		// the language instead of making it guess from a .cpp suffix.
		args = append(args, "-o", ctx.output(0), "-x", "c++", ctx.file(compile, unit.cpp), "-c")
		runCommand("", nil, "", b.context.opts.dryRun, cxx.compiler, args...)
	}

	return task
}

func (b *Builder) compileTarget(ctx *TaskContext, unit *CompileUnit, outDir string) {
	pkg := unit.pkg
	target := unit.target
	output := ctx.outputBase(unit.baseName)
	source := targetSourcePath(pkg, target)
	args := []string{source}

	args = append(args, b.commonCompilerArgs(pkg, output, unit.isHost)...)
	args = append(args, "--crate-name", target.name, "--crate-type", crateType(target))

	suffix := b.crateSuffix(pkg)

	if suffix != "" {
		args = append(args, "--crate-tag", strings.TrimPrefix(suffix, "-"))
	}

	if b.context.cross && !unit.isHost {
		args = append(args, "--target", b.context.target)
	}

	args = append(args, "-C", "emit-cpp-only", "-C", "emit-link-manifest="+ctx.output(unit.linkManifest))

	for _, path := range pkg.buildOutput.linkSearch {
		args = append(args, "-Lnative="+resolveBuildOutputPath(path, outDir))
	}

	for _, lib := range pkg.buildOutput.linkLib {
		args = append(args, "-l", lib)
	}

	for _, cfg := range pkg.buildOutput.cfg {
		args = append(args, "--cfg", cfg)
	}

	for _, flag := range pkg.buildOutput.flags {
		args = append(args, resolveBuildOutputPath(flag, outDir))
	}

	if target.kind != "lib" {
		if lib := packageLibrary(pkg); lib != nil {
			dep := b.units[b.libraryTask(pkg, unit.isHost)]
			args = append(args, "--extern", lib.name+"="+b.crateName(dep))
		}
	}

	if target.edition != "" {
		args = append(args, "--edition", target.edition)
	}

	if target.kind == "test" && target.harness {
		args = append(args, "--test")
	}

	args = append(args, b.crateArgs(ctx, unit, b.compileDependencies(pkg, target.kind == "test"))...)

	env := b.taskEnv(ctx, pkg)
	env["OUT_DIR"] = outDir
	env["CARGO_CRATE_NAME"] = target.name

	for key, value := range pkg.buildOutput.env {
		env[key] = resolveBuildOutputPath(value, outDir)
	}

	for _, command := range pkg.buildOutput.preBuild {
		args := shellCommand(resolveBuildOutputPath(command, outDir))

		runCommand(pkg.dir, env, "", b.context.opts.dryRun, args[0], args[1:]...)
	}

	b.runCompiler("", env, source, args...)

	if !b.context.opts.dryRun {
		makeBuildOutputPortable(ctx.output(unit.linkManifest), outDir)
	}
}

func (b *Builder) compileBuildScript(ctx *TaskContext, unit *CompileUnit) {
	pkg := unit.pkg
	output := ctx.outputBase(unit.baseName)
	args := []string{filepath.Join(pkg.dir, pkg.buildScript)}

	args = append(args, b.commonCompilerArgs(pkg, output, true)...)
	args = append(args, "--crate-name", "build", "--crate-type", "bin", "--edition", pkg.edition)
	args = append(args, "-C", "emit-cpp-only", "-C", "emit-link-manifest="+ctx.output(unit.linkManifest))
	args = append(args, b.crateArgs(ctx, unit, b.buildDependencies(pkg))...)
	b.runCompiler("", b.commonEnv(pkg), absoluteFrom(pkg.dir, pkg.buildScript), args...)
}

func (b *Builder) runBuildScript(ctx *TaskContext, pkg *Package, executable *Task) {
	output := ctx.output(0)
	env := b.taskEnv(ctx, pkg)

	env["OUT_DIR"] = ctx.output(1)
	env["TARGET"] = b.context.target
	env["HOST"] = b.context.host
	env["NUM_JOBS"] = strconv.Itoa(b.context.opts.jobs)
	env["OPT_LEVEL"] = profileOptLevel(b.context.opts.profile)
	env["DEBUG"] = profileDebug(b.context.opts.profile)
	env["PROFILE"] = b.context.opts.profile
	env["RUSTC"] = b.context.compiler

	if len(b.context.opts.libSearch) > 0 {
		env["TRUSTME_LIBDIR"] = absolutePath(b.context.opts.libSearch[0])
	}

	for feature := range pkg.activeFeatures {
		env["CARGO_FEATURE_"+cargoEnvName(feature)] = "1"
	}

	for flag := range b.context.cfg.flags {
		env["CARGO_CFG_"+cargoEnvName(flag)] = "1"
	}

	for key, values := range b.context.cfg.values {
		var list []string

		for value := range values {
			list = append(list, value)
		}

		sort.Strings(list)
		env["CARGO_CFG_"+cargoEnvName(key)] = strings.Join(list, ",")
	}

	if b.context.opts.emitMmir {
		miri := os.Getenv("TRUSTME_MIRI")

		if miri == "" {
			miri = filepath.Join(filepath.Dir(b.context.compiler), "standalone_miri"+executableSuffix())
		}

		runCommand(pkg.dir, env, output, b.context.opts.dryRun, miri,
			ctx.file(executable, 0)+".mir", "--logfile", output+"-miri.log")
	} else {
		runCommand(pkg.dir, env, output, b.context.opts.dryRun, ctx.file(executable, 0))
	}

	if !b.context.opts.dryRun {
		makeBuildOutputPortable(output, ctx.output(1))
	}
}

func (b *Builder) commonCompilerArgs(pkg *Package, output string, isHost bool) []string {
	args := []string{"-o", output}

	if b.context.opts.profile == "release" {
		args = append(args, "-O")
	} else {
		args = append(args, "-g")
	}

	if debugAssertions(b.context.opts.profile) {
		args = append(args, "--cfg", "debug_assertions")
	}

	if b.context.opts.emitMmir {
		args = append(args, "-C", "codegen-type=monomir")
	}

	for _, dir := range b.context.opts.libSearch {
		if isHost && b.context.cross {
			marker := "-" + b.context.target

			if index := strings.LastIndex(dir, marker); index >= 0 {
				dir = dir[:index] + dir[index+len(marker):]
			}
		}

		args = append(args, "-Lnative="+dir)
	}
	args = append(args, b.systemCrateArgs(isHost)...)

	for _, feature := range sortedFeatureKeys(pkg.activeFeatures) {
		args = append(args, "--cfg", "feature=\""+feature+"\"")
	}

	return args
}

func (b *Builder) commonEnv(pkg *Package) map[string]string {
	env := map[string]string{
		"CARGO_MANIFEST_DIR":      absolutePath(pkg.dir),
		"CARGO_MANIFEST_PATH":     pkg.manifestPath,
		"CARGO_PKG_NAME":          pkg.name,
		"CARGO_PKG_VERSION":       pkg.version.string(),
		"CARGO_PKG_VERSION_MAJOR": strconv.Itoa(pkg.version.major),
		"CARGO_PKG_VERSION_MINOR": strconv.Itoa(pkg.version.minor),
		"CARGO_PKG_VERSION_PATCH": strconv.Itoa(pkg.version.patch),
		"CARGO_PKG_VERSION_PRE":   pkg.version.pre,
	}

	for _, dep := range b.mainDependencies(pkg, false) {
		for key, value := range dep.packageRef.buildOutput.downstream {
			env[key] = value
		}
	}

	return env
}

func (b *Builder) taskEnv(ctx *TaskContext, pkg *Package) map[string]string {
	env := b.commonEnv(pkg)

	for _, dep := range b.mainDependencies(pkg, false) {
		script := b.buildScriptRunTask(dep.packageRef)

		if script == nil {
			continue
		}

		outDir := ctx.tree(script, 1)

		for key, value := range dep.packageRef.buildOutput.downstream {
			env[key] = resolveBuildOutputPath(value, outDir)
		}
	}

	return env
}

func (b *Builder) cacheRoot() string {
	dir := b.context.opts.targetDir

	if dir == "" {
		dir = filepath.Join(b.context.workspace.dir, "target")
	}

	return absolutePath(dir)
}

func (b *Builder) unitKey(stage string, pkg *Package, target *Target, isHost bool) string {
	platform := b.context.target

	if isHost {
		platform = b.context.host
	}

	return strings.Join([]string{
		stage, pkg.manifestPath, targetKey(target), platform,
		b.context.opts.profile, b.crateSuffix(pkg),
	}, "|")
}

func (b *Builder) packageInputs(pkg *Package) []string {
	cacheRoot := b.cacheRoot()
	var inputs []string
	throw(filepath.WalkDir(pkg.dir, func(path string, entry os.DirEntry, err error) error {
		if err != nil {
			return err
		}

		if entry.IsDir() {
			if path == cacheRoot || entry.Name() == ".git" {
				return filepath.SkipDir
			}

			return nil
		}

		if entry.Type().IsRegular() {
			inputs = append(inputs, path)
		}

		return nil
	}))
	sort.Strings(inputs)

	return inputs
}

func targetSourcePath(pkg *Package, target *Target) string {
	return absoluteFrom(pkg.dir, target.path)
}

func (b *Builder) targetInputs(pkg *Package, target *Target) []string {
	inputs := b.packageInputs(pkg)
	source := targetSourcePath(pkg, target)

	if !contains(inputs, source) {
		inputs = append(inputs, source)
		sort.Strings(inputs)
	}

	return inputs
}

func (b *Builder) rustSignature(pkg *Package, target *Target, isHost bool) []string {
	signature := []string{
		b.context.compiler,
		b.context.opts.profile,
		b.context.target,
		fmt.Sprintf("host=%t", isHost),
		fmt.Sprintf("emit-mmir=%t", b.context.opts.emitMmir),
		targetKey(target),
		crateType(target),
		b.crateSuffix(pkg),
		"message-format=" + b.context.opts.messageFormat,
	}
	for _, dir := range b.context.opts.libSearch {
		signature = append(signature, "lib-search="+absolutePath(dir))
	}

	for _, feature := range sortedFeatureKeys(pkg.activeFeatures) {
		signature = append(signature, "feature="+feature)
	}

	return append(signature, b.envSignature(b.commonEnv(pkg))...)
}

func (b *Builder) buildScriptRunSignature(pkg *Package) []string {
	signature := []string{
		"target=" + b.context.target,
		"host=" + b.context.host,
		"jobs=" + strconv.Itoa(b.context.opts.jobs),
		"opt-level=" + profileOptLevel(b.context.opts.profile),
		"debug=" + profileDebug(b.context.opts.profile),
		"profile=" + b.context.opts.profile,
		"rustc=" + b.context.compiler,
	}
	if len(b.context.opts.libSearch) > 0 {
		signature = append(signature, "trustme-libdir="+absolutePath(b.context.opts.libSearch[0]))
	}
	for _, feature := range sortedFeatureKeys(pkg.activeFeatures) {
		signature = append(signature, "feature="+feature)
	}
	var flags []string
	for flag := range b.context.cfg.flags {
		flags = append(flags, flag)
	}
	sort.Strings(flags)
	for _, flag := range flags {
		signature = append(signature, "cfg="+flag)
	}
	var keys []string
	for key := range b.context.cfg.values {
		keys = append(keys, key)
	}
	sort.Strings(keys)
	for _, key := range keys {
		var values []string
		for value := range b.context.cfg.values[key] {
			values = append(values, value)
		}
		sort.Strings(values)
		for _, value := range values {
			signature = append(signature, "cfg="+key+"="+value)
		}
	}
	for _, name := range []string{"AR", "CC", "CXX", "PATH", "RUSTC_VERSION", "STD_ENV_ARCH"} {
		signature = append(signature, "env="+name+"="+os.Getenv(name))
	}

	return append(signature, b.envSignature(b.commonEnv(pkg))...)
}

func (b *Builder) envSignature(env map[string]string) []string {
	keys := make([]string, 0, len(env))

	for key := range env {
		keys = append(keys, key)
	}

	sort.Strings(keys)
	result := make([]string, 0, len(keys))

	for _, key := range keys {
		result = append(result, key+"="+env[key])
	}

	return result
}

func (b *Builder) cxxSpec(isHost bool) CxxSpec {
	if isHost {
		return b.context.hostCxx
	}

	return b.context.cxx
}

func (b *Builder) cxxSignature(isHost bool) []string {
	cxx := b.cxxSpec(isHost)
	signature := []string{cxx.compiler, b.context.opts.profile, fmt.Sprintf("intel-asm=%t", cxx.intelAsm)}
	signature = append(signature, cxx.compile...)
	signature = append(signature, cxx.linkPre...)
	signature = append(signature, cxx.linkPost...)

	return signature
}

func (b *Builder) cxxCompileArgs(isHost bool) []string {
	cxx := b.cxxSpec(isHost)
	args := []string{"-std=gnu++20", "-fexceptions", "-fwrapv"}

	if cxx.intelAsm {
		args = append(args, "-masm=intel")
	}

	args = append(args, cxx.compile...)

	if b.context.opts.profile == "release" {
		args = append(args, "-O1")
	} else {
		args = append(args, "-O0", "-g")
	}

	args = append(args, "-fPIC")

	return args
}

func (b *Builder) linkedUnits(root *CompileUnit) []*CompileUnit {
	seen := map[*CompileUnit]bool{root: true}
	var result []*CompileUnit
	var visit func(*Task)
	visit = func(task *Task) {
		for _, dep := range task.deps {
			unit := b.units[dep]

			if unit == nil || seen[unit] || unit.target.procMacro {
				continue
			}

			seen[unit] = true
			result = append(result, unit)
			visit(unit.rs)
		}
	}
	visit(root.rs)

	return result
}

func (b *Builder) linkUnit(ctx *TaskContext, root *CompileUnit, linked []*CompileUnit) {
	cxx := b.cxxSpec(root.isHost)
	args := append([]string(nil), cxx.compile...)
	args = append(args, "-o", ctx.output(0))
	args = append(args, cxx.linkPre...)
	args = append(args, ctx.file(b.codegenTask(root.rs), 0))

	for _, unit := range linked {
		args = append(args, ctx.file(b.codegenTask(unit.rs), 0))
	}

	if root.target.kind == "lib" && !root.target.procMacro {
		args = append(args, "-shared")
	}

	manifests := []*CompileUnit{root}
	manifests = append(manifests, linked...)
	seenObjects := map[string]bool{}

	for _, unit := range manifests {
		outDir := ""

		if unit.target.kind != "build-script" {
			if script := b.buildScriptRunTask(unit.pkg); script != nil {
				outDir = ctx.tree(script, 1)
			}
		}

		data := throw2(os.ReadFile(ctx.file(unit.rs, unit.linkManifest)))

		for _, line := range strings.Split(string(data), "\n") {
			kind, value, ok := strings.Cut(line, "\t")

			if !ok || value == "" {
				continue
			}

			value = resolveBuildOutputPath(value, outDir)

			switch kind {
			case "search":
				args = append(args, "-L", value)
			case "lib":
				if strings.HasPrefix(value, "framework=") {
					args = append(args, "-framework", strings.TrimPrefix(value, "framework="))
				} else {
					args = append(args, "-l", value)
				}
			case "arg":
				args = append(args, value)
			case "object":
				if !seenObjects[value] {
					seenObjects[value] = true
					args = append(args, value)
				}
			}
		}
	}

	args = append(args, cxx.linkPost...)
	runCommand("", nil, "", b.context.opts.dryRun, cxx.compiler, args...)
}

func (b *Builder) crateName(unit *CompileUnit) string {
	return unit.target.name + b.crateSuffix(unit.pkg)
}

func (b *Builder) crateArgs(ctx *TaskContext, root *CompileUnit, direct []*Dependency) []string {
	seen := map[*CompileUnit]bool{}
	var args []string
	var add func(*CompileUnit)
	add = func(unit *CompileUnit) {
		if unit == nil || unit.metadata < 0 || seen[unit] {
			return
		}

		seen[unit] = true
		name := b.crateName(unit)
		args = append(args, "--crate", name+"="+ctx.file(unit.rs, unit.metadata))

		if unit.target.procMacro {
			args = append(args, "--proc-macro", name+"="+ctx.file(b.finalTask(unit.rs), 0))

			return
		}

		for _, dep := range unit.rs.deps {
			add(b.units[dep])
		}
	}

	if root.target.kind != "lib" && root.target.kind != "build-script" {
		add(b.units[b.libraryTask(root.pkg, root.isHost)])
	}

	for _, dep := range direct {
		lib := packageLibrary(dep.packageRef)

		if lib == nil {
			continue
		}

		unit := b.units[b.libraryTask(dep.packageRef, root.isHost || lib.procMacro)]
		add(unit)
		args = append(args, "--extern", rustName(dep.key)+"="+b.crateName(unit))
	}

	return args
}

func (b *Builder) systemCrateArgs(isHost bool) []string {
	var args []string

	for _, artifact := range b.systemCrates(isHost) {
		args = append(args, "--crate", artifact.name+"="+artifact.metadata)
		args = append(args, "--crate-alias", artifact.alias+"="+artifact.name)

		if artifact.object != "" {
			args = append(args, "--crate-object", artifact.name+"="+artifact.object)
		}
	}

	return args
}

func (b *Builder) addSystemInputs(task *Task, isHost bool, objectsOnly bool) {
	for _, artifact := range b.systemCrates(isHost) {
		if objectsOnly && artifact.object != "" {
			task.inputs = append(task.inputs, artifact.object)
		} else if !objectsOnly {
			task.inputs = append(task.inputs, artifact.metadata)
		}
	}
}

func (b *Builder) systemCrates(isHost bool) []ExternalCrateArtifact {
	if isHost && b.systemHostLoaded {
		return b.systemHost
	}

	if !isHost && b.systemTargetLoaded {
		return b.systemTarget
	}

	var result []ExternalCrateArtifact
	byName := map[string]int{}

	for _, dir := range b.systemLibraryDirs(isHost) {
		entries, err := os.ReadDir(dir)

		if err != nil {
			continue
		}

		for _, entry := range entries {
			name := entry.Name()

			if entry.IsDir() || !strings.HasPrefix(name, "lib") || !strings.HasSuffix(name, ".rlib") {
				continue
			}

			metadata := filepath.Join(dir, name)
			command := exec.Command(b.context.compiler, "--crate-name-of", metadata)
			command.Stderr = os.Stderr
			lines := strings.Fields(string(throw2(command.Output())))
			unique := ""

			if len(lines) > 0 {
				// Older/debug compiler builds print phase diagnostics before the
				// requested value. The query contract puts the crate name last.
				unique = lines[len(lines)-1]
			}

			if unique == "" {
				throwFmt("compiler returned an empty crate name for %s", metadata)
			}

			object := metadata + ".o"

			if !fileExists(object) {
				object = ""
			}

			alias, _, _ := strings.Cut(unique, "-")
			artifact := ExternalCrateArtifact{
				alias: alias, name: unique, metadata: metadata, object: object,
			}
			if index, ok := byName[unique]; ok {
				// -Zpublish-deps exposes both a short public filename and the
				// conventional unique fallback as hardlinks to one artifact.
				// Keep one exact table entry and prefer the shorter public path.
				if len(metadata) < len(result[index].metadata) {
					result[index] = artifact
				}
			} else {
				byName[unique] = len(result)
				result = append(result, artifact)
			}
		}
	}
	sort.Slice(result, func(i, j int) bool {
		return result[i].name < result[j].name
	})

	if isHost {
		b.systemHostLoaded = true
		b.systemHost = result
	} else {
		b.systemTargetLoaded = true
		b.systemTarget = result
	}

	return result
}

func (b *Builder) systemLibraryDirs(isHost bool) []string {
	dirs := append([]string(nil), b.context.opts.libSearch...)

	if isHost && b.context.cross {
		marker := "-" + b.context.target

		for index, dir := range dirs {
			if position := strings.LastIndex(dir, marker); position >= 0 {
				dirs[index] = dir[:position] + dir[position+len(marker):]
			}
		}
	}

	return dirs
}

func (b *Builder) mainDependencies(pkg *Package, includeDev bool) []*Dependency {
	all := enabledDependencies(b.context, pkg, includeDev)
	allowed := map[*Dependency]bool{}

	for _, dep := range pkg.dependencies.main {
		allowed[dep] = true
	}

	if includeDev {
		for _, dep := range pkg.dependencies.dev {
			allowed[dep] = true
		}
	}

	for condition, group := range pkg.targetDeps {
		if b.conditionMatches(pkg, condition) {
			for _, dep := range group.main {
				allowed[dep] = true
			}

			if includeDev {
				for _, dep := range group.dev {
					allowed[dep] = true
				}
			}
		}
	}

	result := all[:0]

	for _, dep := range all {
		if allowed[dep] {
			result = append(result, dep)
		}
	}

	return result
}

func (b *Builder) buildDependencies(pkg *Package) []*Dependency {
	var result []*Dependency

	for _, dep := range pkg.dependencies.build {
		if !dep.optional || dep.enabled {
			result = append(result, dep)
		}
	}

	for condition, group := range pkg.targetDeps {
		if b.conditionMatches(pkg, condition) {
			for _, dep := range group.build {
				if !dep.optional || dep.enabled {
					result = append(result, dep)
				}
			}
		}
	}

	return result
}

func (b *Builder) compileDependencies(pkg *Package, includeDev bool) []*Dependency {
	return b.mainDependencies(pkg, includeDev)
}

func (b *Builder) conditionMatches(pkg *Package, condition string) bool {
	if strings.HasPrefix(condition, "cfg(") {
		return b.context.cfg.matches(condition, pkg.activeFeatures)
	}

	return condition == b.context.target
}

func (b *Builder) artifact(pkg *Package, target *Target, isHost bool) string {
	return filepath.Join(b.outputDir(isHost), b.artifactName(pkg, target))
}

func (b *Builder) artifactName(pkg *Package, target *Target) string {
	suffix := b.crateSuffix(pkg)

	if pkg.version == (Version{}) || target.kind != "lib" {
		suffix = ""
	}

	switch target.kind {
	case "lib":
		if target.procMacro {
			return "lib" + target.name + suffix + "-plugin"
		}

		switch crateType(target) {
		case "dylib", "cdylib":
			return "lib" + target.name + suffix + sharedLibrarySuffix()
		case "staticlib":
			return "lib" + target.name + suffix + staticLibrarySuffix()
		}

		return "lib" + target.name + suffix + ".rlib"
	default:
		return target.name + executableSuffix()
	}
}

func (b *Builder) outputDir(isHost bool) string {
	dir := b.context.opts.targetDir

	if dir == "" {
		dir = filepath.Join(b.context.workspace.dir, "target")
	}

	if b.context.cross && !isHost {
		dir = filepath.Join(dir, b.context.target)
	}

	dir = filepath.Join(dir, b.context.opts.profile)

	if b.context.cross && isHost {
		dir = filepath.Join(dir, "host")
	}

	return absolutePath(dir)
}

func (b *Builder) crateSuffix(pkg *Package) string {
	version := strings.ReplaceAll(pkg.version.string(), ".", "_")

	if plus := strings.IndexByte(version, '+'); plus >= 0 {
		version = version[:plus]
	}

	suffix := "-" + version

	if len(pkg.activeFeatures) > 0 {
		keys := make([]string, 0, len(pkg.features))

		for feature := range pkg.features {
			keys = append(keys, feature)
		}

		sort.Strings(keys)
		var mask uint64

		for i, feature := range keys {
			if i == 64 {
				break
			}

			if pkg.activeFeatures[feature] {
				mask |= 1 << i
			}
		}

		suffix += fmt.Sprintf("_H%x", mask)
	}

	return suffix
}

func (b *Builder) buildScriptBase(pkg *Package) string {
	return "build_" + pkg.name + b.crateSuffix(pkg)
}

func (b *Builder) taskName(pkg *Package, target *Target, isHost bool) string {
	name := pkg.name + " v" + pkg.version.string()

	if isHost && b.context.cross {
		name += " (host)"
	}

	if target.kind != "lib" {
		name += " [" + target.kind + " " + target.name + "]"
	}

	return name
}

func packageLibrary(pkg *Package) *Target {
	for _, target := range pkg.targets {
		if target.kind == "lib" {
			return target
		}
	}

	return nil
}

func targetFeaturesEnabled(pkg *Package, target *Target) bool {
	for _, feature := range target.requiredFeatures {
		if !pkg.activeFeatures[feature] {
			return false
		}
	}

	return true
}

func targetKey(target *Target) string {
	return target.kind + "|" + target.name + "|" + target.path
}

func crateType(target *Target) string {
	if target.kind != "lib" {
		return "bin"
	}

	if target.procMacro {
		return "proc-macro"
	}

	if len(target.crateTypes) == 0 || target.crateTypes[0] == "lib" ||
		target.crateTypes[0] == "dylib" && !dylibEnabled() {
		return "rlib"
	}

	switch target.crateTypes[0] {
	case "rlib", "dylib", "proc-macro":
		return target.crateTypes[0]
	case "staticlib", "cdylib":
		return target.crateTypes[0]
	default:
		throwFmt("unsupported crate type %q", target.crateTypes[0])
	}

	return ""
}

func runCommand(dir string, extraEnv map[string]string, logPath string, dryRun bool, name string, args ...string) {
	if _, dump := os.LookupEnv(trustmeCargoDumpCommand); dump {
		fmt.Fprintln(os.Stderr, ">", shellJoin(append([]string{name}, args...)))
	}

	if _, dump := os.LookupEnv(trustmeCargoDumpEnv); dump {
		keys := make([]string, 0, len(extraEnv))

		for key := range extraEnv {
			keys = append(keys, key)
		}

		sort.Strings(keys)

		for _, key := range keys {
			fmt.Fprintf(os.Stderr, "%s=%s\n", key, extraEnv[key])
		}
	}

	if dryRun {
		fmt.Fprintln(os.Stderr, ">", shellJoin(append([]string{name}, args...)))

		return
	}

	cmd := exec.Command(name, args...)

	cmd.Dir = dir
	cmd.Env = os.Environ()

	for key, value := range extraEnv {
		cmd.Env = append(cmd.Env, key+"="+value)
	}

	cmd.Stderr = os.Stderr
	var log *os.File

	if logPath != "" {
		throw(os.MkdirAll(filepath.Dir(logPath), 0o755))
		log = throw2(os.Create(logPath))
		cmd.Stdout = log
	} else {
		cmd.Stdout = os.Stdout
	}

	err := cmd.Run()

	if log != nil {
		throw(log.Close())
	}

	if err != nil {
		if logPath != "" {
			throwFmt("%s failed: %v (stdout: %s)", shellJoin(append([]string{name}, args...)), err, logPath)
		}

		throwFmt("%s failed: %v", shellJoin(append([]string{name}, args...)), err)
	}
}

func (b *Builder) runTest(binary string, args []string) {
	env := b.commonEnv(b.context.root)
	if cargo, err := os.Executable(); err == nil {
		env["CARGO"] = cargo
	}
	if b.context.opts.targetDir != "" {
		env["CARGO_TARGET_DIR"] = absolutePath(b.context.opts.targetDir)
	}
	if b.context.opts.vendorDir != "" {
		env[trustmeCargoVendorDir] = absolutePath(b.context.opts.vendorDir)
	}
	if len(b.context.opts.libSearch) > 0 {
		env[trustmeCargoLibSearch] = strings.Join(b.context.opts.libSearch, string(os.PathListSeparator))
	}
	runCommand(b.context.root.dir, env, "", b.context.opts.dryRun, binary, args...)
}

func shellCommand(command string) []string {
	shell := os.Getenv("SHELL")

	if shell == "" {
		shell = "/bin/sh"
	}

	return []string{shell, "-c", command}
}

func shellJoin(args []string) string {
	parts := make([]string, len(args))

	for i, arg := range args {
		if arg == "" || strings.ContainsAny(arg, " \t\n'\"$`\\") {
			parts[i] = strconv.Quote(arg)
		} else {
			parts[i] = arg
		}
	}

	return strings.Join(parts, " ")
}

func siblingCompiler() string {
	executable := throw2(os.Executable())
	invoked := os.Args[0]

	if resolved, err := exec.LookPath(invoked); err == nil {
		invoked = resolved
	}
	if absolute, err := filepath.Abs(invoked); err == nil {
		invoked = absolute
	}

	return siblingCompilerAt(invoked, executable)
}

func siblingCompilerAt(invoked string, executable string) string {
	candidates := []string{
		filepath.Join(filepath.Dir(invoked), "rustc"),
		filepath.Join(filepath.Dir(invoked), "..", "rustc", "rustc"),
		filepath.Join(filepath.Dir(executable), "..", "rustc", "rustc"),
	}

	for _, candidate := range candidates {
		candidate = filepath.Clean(candidate)
		if fileExists(candidate) {
			return candidate
		}
	}

	return filepath.Clean(candidates[len(candidates)-1])
}

func hostTriple() string {
	arch := map[string]string{"amd64": "x86_64", "386": "i686", "arm64": "aarch64"}[runtime.GOARCH]

	if arch == "" {
		arch = runtime.GOARCH
	}

	switch runtime.GOOS {
	case "linux":
		return arch + "-unknown-linux-gnu"
	case "darwin":
		return arch + "-apple-darwin"
	case "windows":
		panic("Windows hosts are not supported")
	default:
		return arch + "-unknown-" + runtime.GOOS
	}
}

func rustName(name string) string {
	return strings.ReplaceAll(name, "-", "_")
}

func dylibEnabled() bool {
	_, enabled := os.LookupEnv(trustmeCargoDylib)

	return enabled
}

func ignoreToolTimestamps() bool {
	_, enabled := os.LookupEnv(trustmeCargoIgnoreToolTimestamps)

	return enabled
}

func debugAssertions(profile string) bool {
	if _, disabled := os.LookupEnv(trustmeCargoNoDebugAssertions); disabled {
		return false
	}

	return profile != "release"
}

func executableSuffix() string {
	return ""
}

func sharedLibrarySuffix() string {
	if runtime.GOOS == "darwin" {
		return ".dylib"
	}

	return ".so"
}

func staticLibrarySuffix() string {
	return ".a"
}

func profileOptLevel(profile string) string {
	if profile == "release" {
		return "2"
	}

	return "0"
}

func profileDebug(profile string) string {
	if profile == "release" {
		return "0"
	}

	return "1"
}
