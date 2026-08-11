package main

import (
	"bufio"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sort"
	"strconv"
	"strings"
)

type Builder struct {
	context *BuildContext
	tasks   map[string]*Task
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
	compiler := os.Getenv("MRUSTC_PATH")

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
	resolveGraph(context)

	builder := &Builder{context: context, tasks: map[string]*Task{}}
	roots, binaries := builder.rootTasks()

	runTasks(roots, opts.jobs)

	if opts.command == "test" && !opts.noRun {
		for _, binary := range binaries {
			runTest(binary, opts.testArgs, opts.dryRun)
		}
	}

	return binaries
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

func (b *Builder) rootTasks() ([]*Task, []string) {
	root := b.context.root
	isHost := !b.context.cross

	var tasks []*Task
	var binaries []string

	selectors := b.context.opts.selectors

	explicit := selectors.lib || selectors.bins || len(selectors.bin) > 0 || selectors.tests ||
		len(selectors.test) > 0 || selectors.examples || len(selectors.example) > 0 ||
		selectors.benches || len(selectors.bench) > 0

	if b.context.opts.command == "build" {
		if !explicit || selectors.lib {
			if task := b.libraryTask(root, isHost); task != nil {
				tasks = append(tasks, b.finalTask(task))
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

				tasks = append(tasks, b.finalTask(task))

				if target.kind != "lib" {
					binaries = append(binaries, b.artifact(root, target, isHost))
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

			tasks = append(tasks, b.finalTask(task))
			binaries = append(binaries, b.artifact(root, &target, isHost))
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

				tasks = append(tasks, b.finalTask(task))
				binaries = append(binaries, b.artifact(root, &copy, isHost))
			}
		}
	}

	if len(tasks) == 0 {
		throwFmt("package %s has no selected targets", root.name)
	}

	return tasks, binaries
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
	key := fmt.Sprintf("compile|%p|%s|%t", pkg, targetKey(target), isHost)

	if task := b.tasks[key]; task != nil {
		return task
	}

	output := b.artifact(pkg, target, isHost)

	task := &Task{
		name:    b.taskName(pkg, target, isHost),
		verb:    "Compiling",
		inputs:  []string{pkg.manifestPath, filepath.Join(pkg.dir, target.path), b.context.compiler},
		outputs: []string{output},
	}

	if ignoreToolTimestamps() {
		task.inputs = task.inputs[:len(task.inputs)-1]
	}

	b.tasks[key] = task

	if target.kind != "lib" {
		if libTask := b.libraryTask(pkg, isHost); libTask != nil {
			task.deps = append(task.deps, b.finalTask(libTask))
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
			task.deps = append(task.deps, b.finalTask(depTask))
		}
	}

	if script := b.buildScriptRunTask(pkg); script != nil {
		task.deps = append(task.deps, script)
		task.inputs = append(task.inputs, script.outputs...)
	}

	task.action = func() {
		b.ensureBuildOutput(pkg)
		b.compileTarget(pkg, target, isHost, output)
	}

	return task
}

func (b *Builder) buildScriptCompileTask(pkg *Package) *Task {
	if pkg.buildScript == "" {
		return nil
	}

	key := fmt.Sprintf("script-compile|%p", pkg)

	if task := b.tasks[key]; task != nil {
		return task
	}

	output := b.buildScriptExe(pkg)

	task := &Task{
		name:    pkg.name + " v" + pkg.version.string() + " (build script)",
		verb:    "Compiling",
		inputs:  []string{pkg.manifestPath, filepath.Join(pkg.dir, pkg.buildScript), b.context.compiler},
		outputs: []string{output},
	}

	if ignoreToolTimestamps() {
		task.inputs = task.inputs[:len(task.inputs)-1]
	}

	b.tasks[key] = task

	for _, dep := range b.buildDependencies(pkg) {
		if depTask := b.libraryTask(dep.packageRef, true); depTask != nil {
			task.deps = append(task.deps, b.finalTask(depTask))
		}
	}

	task.action = func() {
		b.compileBuildScript(pkg, output)
	}

	return task
}

func (b *Builder) buildScriptRunTask(pkg *Package) *Task {
	if pkg.buildScript == "" {
		return nil
	}

	key := fmt.Sprintf("script-run|%p", pkg)

	if task := b.tasks[key]; task != nil {
		return task
	}

	output := b.buildScriptLog(pkg)

	task := &Task{
		name:    pkg.name + " v" + pkg.version.string() + " (build script run)",
		verb:    "Running",
		inputs:  []string{pkg.manifestPath, filepath.Join(pkg.dir, pkg.buildScript)},
		outputs: []string{output},
	}

	b.tasks[key] = task
	task.deps = append(task.deps, b.buildScriptCompileTask(pkg))

	for _, dep := range b.mainDependencies(pkg, false) {
		if depTask := b.libraryTask(dep.packageRef, !b.context.cross); depTask != nil {
			task.deps = append(task.deps, b.finalTask(depTask))
		}
	}

	task.action = func() {
		b.runBuildScript(pkg, output)
	}

	return task
}

func (b *Builder) finalTask(compile *Task) *Task {
	if compile == nil || !deferredCodegen() {
		return compile
	}

	key := "codegen|" + compile.name

	if task := b.tasks[key]; task != nil {
		return task
	}

	commandFile := compile.outputs[0] + "-codegen.sh"
	output := compile.outputs[0]

	if strings.HasSuffix(output, ".rlib") {
		output += ".o"
	}

	task := &Task{
		name:    compile.name + " (codegen)",
		verb:    "Codegen",
		deps:    []*Task{compile},
		inputs:  []string{commandFile},
		outputs: []string{output},
		action: func() {
			if b.context.opts.dryRun {
				fmt.Fprintln(os.Stderr, "> codegen", commandFile)

				return
			}

			file := throw2(os.Open(commandFile))
			scanner := bufio.NewScanner(file)

			if !scanner.Scan() {
				throwFmt("%s: empty codegen command", commandFile)
			}

			line := strings.TrimSpace(scanner.Text())

			throw(file.Close())

			command := shellCommand(line)

			runCommand("", nil, "", b.context.opts.dryRun, command[0], command[1:]...)
		},
	}

	b.tasks[key] = task

	return task
}

func (b *Builder) compileTarget(pkg *Package, target *Target, isHost bool, output string) {
	args := []string{filepath.Join(pkg.dir, target.path)}

	args = append(args, b.commonCompilerArgs(pkg, output, isHost)...)
	args = append(args, "--crate-name", target.name, "--crate-type", crateType(target))

	suffix := b.crateSuffix(pkg)

	if suffix != "" {
		args = append(args, "--crate-tag", strings.TrimPrefix(suffix, "-"))
	}

	if b.context.cross && !isHost {
		args = append(args, "--target", b.context.target)
	}

	if deferredCodegen() {
		args = append(args, "-C", "emit-build-command="+output+"-codegen.sh")
	}

	for _, path := range pkg.buildOutput.linkSearch {
		args = append(args, "-L", path)
	}

	for _, lib := range pkg.buildOutput.linkLib {
		args = append(args, "-l", lib)
	}

	for _, cfg := range pkg.buildOutput.cfg {
		args = append(args, "--cfg", cfg)
	}

	args = append(args, pkg.buildOutput.flags...)

	if target.kind != "lib" {
		if lib := packageLibrary(pkg); lib != nil {
			args = append(args, "--extern", lib.name+"="+b.artifact(pkg, lib, isHost))
		}
	}

	if target.edition != "" {
		args = append(args, "--edition", target.edition)
	}

	if target.kind == "test" && target.harness {
		args = append(args, "--test")
	}

	for _, dep := range b.compileDependencies(pkg, target.kind == "test") {
		lib := packageLibrary(dep.packageRef)

		if lib == nil {
			continue
		}

		depHost := isHost || lib.procMacro

		args = append(args, "--extern", rustName(dep.key)+"="+b.artifact(dep.packageRef, lib, depHost))
	}

	env := b.commonEnv(pkg)

	env["OUT_DIR"] = absolutePath(b.buildScriptOutDir(pkg))
	env["CARGO_CRATE_NAME"] = target.name

	for key, value := range pkg.buildOutput.env {
		env[key] = value
	}

	for _, command := range pkg.buildOutput.preBuild {
		args := shellCommand(command)

		runCommand(pkg.dir, env, "", b.context.opts.dryRun, args[0], args[1:]...)
	}

	runCommand("", env, output+"_dbg.txt", b.context.opts.dryRun, b.context.compiler, args...)
}

func (b *Builder) compileBuildScript(pkg *Package, output string) {
	args := []string{filepath.Join(pkg.dir, pkg.buildScript)}

	args = append(args, b.commonCompilerArgs(pkg, output, true)...)
	args = append(args, "--crate-name", "build", "--crate-type", "bin", "--edition", pkg.edition)

	for _, dep := range b.buildDependencies(pkg) {
		lib := packageLibrary(dep.packageRef)

		if lib != nil {
			args = append(args, "--extern", rustName(dep.key)+"="+b.artifact(dep.packageRef, lib, true))
		}
	}

	runCommand("", b.commonEnv(pkg), output+"_dbg.txt", b.context.opts.dryRun, b.context.compiler, args...)
}

func (b *Builder) runBuildScript(pkg *Package, output string) {
	throw(os.MkdirAll(b.buildScriptOutDir(pkg), 0o755))

	env := b.commonEnv(pkg)

	env["OUT_DIR"] = absolutePath(b.buildScriptOutDir(pkg))
	env["TARGET"] = b.context.target
	env["HOST"] = b.context.host
	env["NUM_JOBS"] = strconv.Itoa(b.context.opts.jobs)
	env["OPT_LEVEL"] = profileOptLevel(b.context.opts.profile)
	env["DEBUG"] = profileDebug(b.context.opts.profile)
	env["PROFILE"] = b.context.opts.profile
	env["RUSTC"] = b.context.compiler

	if len(b.context.opts.libSearch) > 0 {
		env["MRUSTC_LIBDIR"] = absolutePath(b.context.opts.libSearch[0])
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
		miri := os.Getenv("MRUSTC_MIRI")

		if miri == "" {
			miri = filepath.Join(filepath.Dir(b.context.compiler), "standalone_miri"+executableSuffix())
		}

		runCommand(pkg.dir, env, output, b.context.opts.dryRun, miri,
			absolutePath(b.buildScriptExe(pkg))+".mir", "--logfile", absolutePath(output)+"-miri.log")
	} else {
		runCommand(pkg.dir, env, output, b.context.opts.dryRun, absolutePath(b.buildScriptExe(pkg)))
	}

	if !b.context.opts.dryRun {
		loadBuildScriptOutput(pkg, output)
	}
}

func (b *Builder) commonCompilerArgs(pkg *Package, output string, isHost bool) []string {
	throw(os.MkdirAll(filepath.Dir(output), 0o755))

	args := []string{"-o", output, "-C", "emit-depfile=" + output + ".d"}

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

		args = append(args, "-L", dir)
	}

	args = append(args, "-L", b.outputDir(isHost))

	if b.context.cross && !isHost {
		args = append(args, "-L", b.outputDir(true))
	}

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

func (b *Builder) ensureBuildOutput(pkg *Package) {
	if pkg.buildScript == "" || b.context.opts.dryRun {
		return
	}

	if len(pkg.buildOutput.linkSearch) == 0 && len(pkg.buildOutput.cfg) == 0 && len(pkg.buildOutput.env) == 0 {
		loadBuildScriptOutput(pkg, b.buildScriptLog(pkg))
	}
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
	dir := b.outputDir(isHost)
	suffix := b.crateSuffix(pkg)

	if pkg.version == (Version{}) || target.kind != "lib" {
		suffix = ""
	}

	switch target.kind {
	case "lib":
		if target.procMacro {
			return filepath.Join(dir, "lib"+target.name+suffix+"-plugin")
		}

		switch crateType(target) {
		case "dylib", "cdylib":
			return filepath.Join(dir, "lib"+target.name+suffix+sharedLibrarySuffix())
		case "staticlib":
			return filepath.Join(dir, "lib"+target.name+suffix+staticLibrarySuffix())
		}

		return filepath.Join(dir, "lib"+target.name+suffix+".rlib")
	default:
		return filepath.Join(dir, target.name+executableSuffix())
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

func (b *Builder) buildScriptExe(pkg *Package) string {
	return filepath.Join(b.outputDir(true), b.buildScriptBase(pkg)+"_run"+executableSuffix())
}

func (b *Builder) buildScriptOutDir(pkg *Package) string {
	return filepath.Join(b.outputDir(true), b.buildScriptBase(pkg))
}

func (b *Builder) buildScriptLog(pkg *Package) string {
	return filepath.Join(b.outputDir(true), b.buildScriptBase(pkg)+".txt")
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
	if _, dump := os.LookupEnv("CARGO_MRUSTC_DUMP_ENV"); dump {
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

func runTest(binary string, args []string, dryRun bool) {
	runCommand("", nil, "", dryRun, binary, args...)
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

	return filepath.Clean(filepath.Join(filepath.Dir(executable), "..", "rustc", "rustc"))
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

func deferredCodegen() bool {
	_, enabled := os.LookupEnv("CARGO_MRUSTC_DEFER_CODEGEN")

	return enabled
}

func dylibEnabled() bool {
	_, enabled := os.LookupEnv("CARGO_MRUSTC_DYLIB")

	return enabled
}

func ignoreToolTimestamps() bool {
	_, enabled := os.LookupEnv("CARGO_MRUSTC_IGNORE_TOOL_TIMESTAMPS")

	return enabled
}

func debugAssertions(profile string) bool {
	if _, disabled := os.LookupEnv("CARGO_MRUSTC_NO_DEBUG_ASSERTIONS"); disabled {
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
