package main

import (
	"os"
	"path/filepath"
	"sort"
	"strings"

	"github.com/BurntSushi/toml"
)

func readToml(path string) map[string]any {
	value := map[string]any{}
	_, err := toml.DecodeFile(path, &value)

	if err != nil {
		throwFmt("%s: %v", path, err)
	}

	return value
}

func mapValue(value any) map[string]any {
	if value == nil {
		return nil
	}

	if result, ok := value.(map[string]any); ok {
		return result
	}

	if result, ok := value.(map[string]interface{}); ok {
		return result
	}

	return nil
}

func stringValue(value any) string {
	if result, ok := value.(string); ok {
		return result
	}

	return ""
}

func boolValue(value any, fallback bool) bool {
	if result, ok := value.(bool); ok {
		return result
	}

	return fallback
}

func stringsValue(value any) []string {
	items, ok := value.([]map[string]any)

	if ok {
		_ = items

		return nil
	}

	raw, ok := value.([]any)

	if !ok {
		if typed, ok := value.([]string); ok {
			return append([]string(nil), typed...)
		}

		return nil
	}

	result := make([]string, 0, len(raw))

	for _, item := range raw {
		if text, ok := item.(string); ok {
			result = append(result, text)
		}
	}

	return result
}

func tableArray(value any) []map[string]any {
	if result, ok := value.([]map[string]any); ok {
		return result
	}

	raw, ok := value.([]any)

	if !ok {
		return nil
	}

	result := make([]map[string]any, 0, len(raw))

	for _, item := range raw {
		if table := mapValue(item); table != nil {
			result = append(result, table)
		}
	}

	return result
}

func findWorkspace(manifestPath string) *Workspace {
	manifestPath = absolutePath(manifestPath)

	doc := readToml(manifestPath)
	packageTable := mapValue(doc["package"])

	if packageTable != nil {
		if path := stringValue(packageTable["workspace"]); path != "" {
			return loadWorkspace(filepath.Join(filepath.Dir(manifestPath), path, "Cargo.toml"))
		}
	}

	for dir := filepath.Dir(manifestPath); ; dir = filepath.Dir(dir) {
		candidate := filepath.Join(dir, "Cargo.toml")

		if fileExists(candidate) {
			candidateDoc := readToml(candidate)

			if mapValue(candidateDoc["workspace"]) != nil {
				return loadWorkspace(candidate)
			}
		}

		parent := filepath.Dir(dir)

		if parent == dir {
			break
		}
	}

	return &Workspace{
		dir:          filepath.Dir(manifestPath),
		manifestPath: manifestPath,
		dependencies: map[string]*Dependency{},
		patches:      map[string]string{},
	}
}

func loadWorkspace(path string) *Workspace {
	path = absolutePath(path)

	doc := readToml(path)
	table := mapValue(doc["workspace"])

	workspace := &Workspace{
		dir:          filepath.Dir(path),
		manifestPath: path,
		dependencies: map[string]*Dependency{},
		patches:      map[string]string{},
	}

	if table == nil {
		return workspace
	}

	workspace.resolver = stringValue(table["resolver"])
	workspace.members = stringsValue(table["members"])
	workspace.defaultMembers = stringsValue(table["default-members"])
	workspace.exclude = stringsValue(table["exclude"])

	packageTable := mapValue(table["package"])

	if packageTable != nil {
		workspace.edition = stringValue(packageTable["edition"])
	}

	for key, value := range mapValue(table["dependencies"]) {
		workspace.dependencies[key] = parseDependency(key, value, workspace.dir, nil)
	}

	patch := mapValue(doc["patch"])
	cratesIO := mapValue(patch["crates-io"])

	for key, value := range cratesIO {
		if path := stringValue(mapValue(value)["path"]); path != "" {
			workspace.patches[key] = absoluteFrom(workspace.dir, path)
		}
	}

	return workspace
}

func newRepository(workspace *Workspace, vendorDir string) *Repository {
	if vendorDir != "" {
		vendorDir = absolutePath(vendorDir)
	}

	result := &Repository{
		workspace: workspace,
		vendorDir: vendorDir,
		byName:    map[string][]*Package{},
		byPath:    map[string]*Package{},
		patches:   workspace.patches,
		locked:    map[string]map[Version]bool{},
	}

	lockPath := filepath.Join(workspace.dir, "Cargo.lock")

	if fileExists(lockPath) {
		for _, pkg := range parseLock(lockPath) {
			if result.locked[pkg.name] == nil {
				result.locked[pkg.name] = map[Version]bool{}
			}

			result.locked[pkg.name][parseVersion(pkg.version)] = true
		}
	}

	if vendorDir != "" {
		entries := throw2(os.ReadDir(vendorDir))

		for _, entry := range entries {
			manifest := filepath.Join(vendorDir, entry.Name(), "Cargo.toml")

			if !fileExists(manifest) {
				continue
			}

			result.loadPath(manifest)
		}
	}

	for _, packages := range result.byName {
		sort.Slice(packages, func(i, j int) bool {
			return compareVersion(packages[i].version, packages[j].version) > 0
		})
	}

	return result
}

func (r *Repository) loadPath(path string) *Package {
	path = absolutePath(path)

	if pkg := r.byPath[path]; pkg != nil {
		return pkg
	}

	doc := readToml(path)

	pkg := parsePackage(path, doc, r.workspace)

	r.byPath[path] = pkg
	r.byName[pkg.name] = append(r.byName[pkg.name], pkg)

	return pkg
}

func (r *Repository) resolve(dep *Dependency, from *Package) *Package {
	if dep.packageRef != nil {
		return dep.packageRef
	}

	if patch := r.patches[dep.name]; patch != "" {
		dep.packageRef = r.loadPath(filepath.Join(patch, "Cargo.toml"))

		return dep.packageRef
	}

	if dep.path != "" {
		dep.packageRef = r.loadPath(filepath.Join(dep.path, "Cargo.toml"))

		return dep.packageRef
	}

	for _, pkg := range r.byName[dep.name] {
		if dep.version.accepts(pkg.version) && !r.isVendored(pkg) {
			dep.packageRef = pkg

			return pkg
		}
	}

	for _, pkg := range r.byName[dep.name] {
		locked := r.locked[dep.name]

		if dep.version.accepts(pkg.version) && (len(locked) == 0 || locked[pkg.version]) {
			dep.packageRef = pkg

			return pkg
		}
	}

	if strings.HasPrefix(dep.name, "rustc-std-workspace-") {
		name := strings.TrimPrefix(dep.name, "rustc-std-workspace-")

		pkg := &Package{
			name:           dep.name,
			version:        Version{major: 1, minor: 99},
			edition:        "2015",
			activeFeatures: map[string]bool{},
			features:       map[string][]string{},
			magic:          true,
			targetDeps:     map[string]Dependencies{},
			targets:        []*Target{{kind: "lib", name: name, path: "src/lib.rs", crateTypes: []string{"rlib"}}},
		}

		dep.packageRef = pkg

		return pkg
	}

	throwFmt("unable to resolve %s %s required by %s", dep.name, dep.version.string(), from.name)

	return nil
}

func (r *Repository) isVendored(pkg *Package) bool {
	if r.vendorDir == "" {
		return false
	}

	vendorDir := filepath.Clean(r.vendorDir) + string(filepath.Separator)

	return strings.HasPrefix(filepath.Clean(pkg.dir)+string(filepath.Separator), vendorDir)
}

func parsePackage(path string, doc map[string]any, workspace *Workspace) *Package {
	dir := filepath.Dir(path)
	table := mapValue(doc["package"])

	if table == nil {
		throwFmt("%s has no [package] table", path)
	}

	pkg := &Package{
		dir:            dir,
		manifestPath:   path,
		name:           stringValue(table["name"]),
		edition:        stringValue(table["edition"]),
		links:          stringValue(table["links"]),
		targetDeps:     map[string]Dependencies{},
		features:       map[string][]string{},
		activeFeatures: map[string]bool{},
		buildOutput: BuildScriptOutput{
			env:        map[string]string{},
			downstream: map[string]string{},
		},
	}

	if pkg.name == "" {
		throwFmt("%s has no package.name", path)
	}

	versionText := stringValue(table["version"])

	if versionText == "" {
		versionText = "0.0.0"
	}

	pkg.version = parseVersion(versionText)

	if editionTable := mapValue(table["edition"]); editionTable != nil && boolValue(editionTable["workspace"], false) {
		pkg.edition = workspace.edition
	}

	if pkg.edition == "" {
		pkg.edition = "2015"
	}

	switch value := table["build"].(type) {
	case string:
		pkg.buildScript = value
	case bool:
		if value {
			pkg.buildScript = "build.rs"
		}
	default:
		if fileExists(filepath.Join(dir, "build.rs")) {
			pkg.buildScript = "build.rs"
		}
	}

	pkg.dependencies.main = parseDependencies(doc["dependencies"], dir, workspace)
	pkg.dependencies.build = parseDependencies(doc["build-dependencies"], dir, workspace)
	pkg.dependencies.dev = parseDependencies(doc["dev-dependencies"], dir, workspace)
	parseTargetDependencies(pkg, doc["target"], workspace)
	parseFeatures(pkg, doc["features"])
	parseTargets(pkg, doc, table)

	return pkg
}

func parseDependencies(value any, dir string, workspace *Workspace) []*Dependency {
	table := mapValue(value)
	result := make([]*Dependency, 0, len(table))
	keys := make([]string, 0, len(table))

	for key := range table {
		keys = append(keys, key)
	}

	sort.Strings(keys)

	for _, key := range keys {
		result = append(result, parseDependency(key, table[key], dir, workspace))
	}

	return result
}

func parseDependency(key string, value any, dir string, workspace *Workspace) *Dependency {
	dep := &Dependency{key: key, name: key, defaultFeatures: true}

	if version, ok := value.(string); ok {
		dep.version = parseVersionSpec(version)

		return dep
	}

	table := mapValue(value)

	if boolValue(table["workspace"], false) {
		if workspace == nil || workspace.dependencies[key] == nil {
			throwFmt("workspace dependency %q is not defined", key)
		}

		copy := *workspace.dependencies[key]

		copy.features = append([]string(nil), copy.features...)
		dep = &copy
	}

	if name := stringValue(table["package"]); name != "" {
		dep.name = name
	}

	if version := stringValue(table["version"]); version != "" {
		dep.version = parseVersionSpec(version)
	}

	if path := stringValue(table["path"]); path != "" {
		dep.path = absoluteFrom(dir, path)
	}

	dep.git = stringValue(table["git"])
	dep.branch = stringValue(table["branch"])
	dep.optional = boolValue(table["optional"], dep.optional)
	dep.public = boolValue(table["public"], dep.public)
	dep.defaultFeatures = boolValue(table["default-features"], dep.defaultFeatures)
	dep.defaultFeatures = boolValue(table["default_features"], dep.defaultFeatures)
	dep.features = append(dep.features, stringsValue(table["features"])...)

	return dep
}

func parseTargetDependencies(pkg *Package, value any, workspace *Workspace) {
	for condition, raw := range mapValue(value) {
		table := mapValue(raw)

		deps := Dependencies{
			main:  parseDependencies(table["dependencies"], pkg.dir, workspace),
			build: parseDependencies(table["build-dependencies"], pkg.dir, workspace),
			dev:   parseDependencies(table["dev-dependencies"], pkg.dir, workspace),
		}

		pkg.targetDeps[condition] = deps
	}
}

func parseFeatures(pkg *Package, value any) {
	for name, raw := range mapValue(value) {
		features := stringsValue(raw)

		if name == "default" {
			pkg.defaultFeature = features
		} else {
			pkg.features[name] = features
		}
	}

	explicit := map[string]bool{}

	for _, features := range pkg.features {
		for _, feature := range features {
			if strings.HasPrefix(feature, "dep:") {
				explicit[strings.TrimPrefix(feature, "dep:")] = true
			}
		}
	}

	for _, dep := range allDependencies(pkg) {
		if dep.optional && !explicit[dep.key] {
			if _, exists := pkg.features[dep.key]; !exists {
				pkg.features[dep.key] = []string{"dep:" + dep.key}
			}
		}
	}
}

func parseTargets(pkg *Package, doc, packageTable map[string]any) {
	if table := mapValue(doc["lib"]); table != nil {
		pkg.targets = append(pkg.targets, parseTarget("lib", table, pkg))
	} else if boolValue(packageTable["autolib"], true) && fileExists(filepath.Join(pkg.dir, "src", "lib.rs")) {
		pkg.targets = append(pkg.targets, parseTarget("lib", map[string]any{}, pkg))
	}

	for _, table := range tableArray(doc["bin"]) {
		pkg.targets = append(pkg.targets, parseTarget("bin", table, pkg))
	}

	if boolValue(packageTable["autobins"], true) {
		addAutomaticBins(pkg)
	}

	for _, kind := range []string{"test", "bench", "example"} {
		for _, table := range tableArray(doc[kind]) {
			pkg.targets = append(pkg.targets, parseTarget(kind, table, pkg))
		}
	}
}

func parseTarget(kind string, table map[string]any, pkg *Package) *Target {
	target := &Target{
		kind:             kind,
		name:             stringValue(table["name"]),
		path:             stringValue(table["path"]),
		edition:          stringValue(table["edition"]),
		crateTypes:       stringsValue(table["crate-type"]),
		test:             boolValue(table["test"], true),
		doctest:          boolValue(table["doctest"], true),
		bench:            boolValue(table["bench"], true),
		doc:              boolValue(table["doc"], true),
		procMacro:        boolValue(table["proc-macro"], boolValue(table["proc_macro"], false)),
		plugin:           boolValue(table["plugin"], false),
		harness:          boolValue(table["harness"], true),
		requiredFeatures: stringsValue(table["required-features"]),
	}

	if target.name == "" {
		target.name = strings.ReplaceAll(pkg.name, "-", "_")
	}

	if target.path == "" {
		switch kind {
		case "lib":
			target.path = "src/lib.rs"
		case "bin":
			target.path = "src/main.rs"
		case "test":
			target.path = filepath.Join("tests", target.name+".rs")
		case "bench":
			target.path = filepath.Join("benches", target.name+".rs")
		case "example":
			target.path = filepath.Join("examples", target.name+".rs")
		}
	}

	if target.edition == "" {
		target.edition = pkg.edition
	}

	if len(target.crateTypes) == 0 && kind == "lib" {
		if target.procMacro {
			target.crateTypes = []string{"proc-macro"}
		} else {
			target.crateTypes = []string{"rlib"}
		}
	}

	return target
}

func addAutomaticBins(pkg *Package) {
	seen := map[string]bool{}

	for _, target := range pkg.targets {
		if target.kind == "bin" {
			seen[target.name] = true
		}
	}

	mainPath := filepath.Join(pkg.dir, "src", "main.rs")

	if fileExists(mainPath) && !seen[strings.ReplaceAll(pkg.name, "-", "_")] {
		pkg.targets = append(pkg.targets, parseTarget("bin", map[string]any{}, pkg))
	}

	binDir := filepath.Join(pkg.dir, "src", "bin")
	entries, err := os.ReadDir(binDir)

	if err != nil {
		return
	}

	for _, entry := range entries {
		name := strings.TrimSuffix(entry.Name(), ".rs")
		path := filepath.Join("src", "bin", entry.Name())

		if entry.IsDir() {
			path = filepath.Join("src", "bin", entry.Name(), "main.rs")
		}

		if seen[name] || !fileExists(filepath.Join(pkg.dir, path)) {
			continue
		}

		pkg.targets = append(pkg.targets, parseTarget("bin", map[string]any{"name": name, "path": path}, pkg))
	}
}

func allDependencies(pkg *Package) []*Dependency {
	result := append([]*Dependency{}, pkg.dependencies.main...)

	result = append(result, pkg.dependencies.build...)
	result = append(result, pkg.dependencies.dev...)

	for _, deps := range pkg.targetDeps {
		result = append(result, deps.main...)
		result = append(result, deps.build...)
		result = append(result, deps.dev...)
	}

	return result
}

func absolutePath(path string) string {
	return throw2(filepath.Abs(path))
}

func absoluteFrom(dir, path string) string {
	if filepath.IsAbs(path) {
		return filepath.Clean(path)
	}

	return absolutePath(filepath.Join(dir, path))
}

func fileExists(path string) bool {
	info, err := os.Stat(path)

	return err == nil && !info.IsDir()
}

func (s VersionSpec) string() string {
	if len(s.bounds) == 0 {
		return "*"
	}

	parts := make([]string, 0, len(s.bounds))

	for _, bound := range s.bounds {
		parts = append(parts, bound.op+bound.version.string())
	}

	return strings.Join(parts, ", ")
}
