package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"sort"
)

type MetadataOptions struct {
	manifestPath string
	targetDir    string
	format       string
	noDeps       bool
}

type MetadataOutput struct {
	Packages                []MetadataPackage `json:"packages"`
	WorkspaceMembers        []string          `json:"workspace_members"`
	WorkspaceDefaultMembers []string          `json:"workspace_default_members"`
	Resolve                 any               `json:"resolve"`
	TargetDirectory         string            `json:"target_directory"`
	Version                 int               `json:"version"`
	WorkspaceRoot           string            `json:"workspace_root"`
	Metadata                any               `json:"metadata"`
}

type MetadataPackage struct {
	Name          string               `json:"name"`
	Version       string               `json:"version"`
	ID            string               `json:"id"`
	License       any                  `json:"license"`
	LicenseFile   any                  `json:"license_file"`
	Description   any                  `json:"description"`
	Source        any                  `json:"source"`
	Dependencies  []MetadataDependency `json:"dependencies"`
	Targets       []MetadataTarget     `json:"targets"`
	Features      map[string][]string  `json:"features"`
	ManifestPath  string               `json:"manifest_path"`
	Metadata      any                  `json:"metadata"`
	Publish       any                  `json:"publish"`
	Authors       []string             `json:"authors"`
	Categories    []string             `json:"categories"`
	Keywords      []string             `json:"keywords"`
	Readme        any                  `json:"readme"`
	Repository    any                  `json:"repository"`
	Homepage      any                  `json:"homepage"`
	Documentation any                  `json:"documentation"`
	Edition       string               `json:"edition"`
	Links         any                  `json:"links"`
	DefaultRun    any                  `json:"default_run"`
	RustVersion   any                  `json:"rust_version"`
}

type MetadataDependency struct {
	Name                string   `json:"name"`
	Source              any      `json:"source"`
	Req                 string   `json:"req"`
	Kind                any      `json:"kind"`
	Rename              any      `json:"rename"`
	Optional            bool     `json:"optional"`
	UsesDefaultFeatures bool     `json:"uses_default_features"`
	Features            []string `json:"features"`
	Target              any      `json:"target"`
	Registry            any      `json:"registry"`
	Path                any      `json:"path"`
}

type MetadataTarget struct {
	Kind             []string `json:"kind"`
	CrateTypes       []string `json:"crate_types"`
	Name             string   `json:"name"`
	SrcPath          string   `json:"src_path"`
	Edition          string   `json:"edition"`
	Doc              bool     `json:"doc"`
	Doctest          bool     `json:"doctest"`
	Test             bool     `json:"test"`
	RequiredFeatures []string `json:"required-features"`
}

func cmdMetadata(args []string) {
	opts := parseMetadataOptions(args)
	output := buildMetadata(opts)
	encoder := json.NewEncoder(os.Stdout)
	throw(encoder.Encode(output))
}

func parseMetadataOptions(args []string) MetadataOptions {
	args = expandOptionEquals(args, map[string]bool{
		"--manifest-path": true, "--format-version": true, "--target-dir": true,
		"--filter-platform": true, "--features": true, "--color": true,
		"--config": true,
	})

	opts := MetadataOptions{manifestPath: "Cargo.toml", format: "1"}

	for i := 0; i < len(args); i++ {
		arg := args[i]
		value := func() string {
			if i+1 >= len(args) {
				throwFmt("option %s requires a value", arg)
			}
			i++
			return args[i]
		}

		switch arg {
		case "--manifest-path":
			opts.manifestPath = value()
		case "--target-dir":
			opts.targetDir = value()
		case "--format-version":
			opts.format = value()
		case "--no-deps":
			opts.noDeps = true
		case "--filter-platform", "--features", "--color", "--config":
			_ = value()
		case "--all-features", "--no-default-features", "--locked", "--offline", "--frozen", "-q", "--quiet", "-v", "--verbose":
		default:
			throwFmt("unexpected argument for metadata: %s", arg)
		}
	}

	if opts.format != "1" {
		throwFmt("unsupported metadata format version: %s", opts.format)
	}

	return opts
}

func buildMetadata(opts MetadataOptions) MetadataOutput {
	manifestPath := absolutePath(opts.manifestPath)
	workspace := findWorkspace(manifestPath)
	repository := newRepository(workspace, "")
	paths := workspaceMemberManifests(workspace)

	if mapValue(readToml(workspace.manifestPath)["package"]) != nil {
		paths = append(paths, workspace.manifestPath)
	}
	if mapValue(readToml(manifestPath)["package"]) != nil {
		paths = append(paths, manifestPath)
	}
	paths = uniqueSortedPaths(paths)

	packages := make([]MetadataPackage, 0, len(paths))
	ids := map[string]string{}
	for _, path := range paths {
		pkg := repository.loadPath(path)
		metadata := metadataPackage(pkg, readToml(path))
		packages = append(packages, metadata)
		ids[path] = metadata.ID
	}

	defaultPaths := workspacePatterns(workspace, workspace.defaultMembers)
	if len(defaultPaths) == 0 {
		if mapValue(readToml(workspace.manifestPath)["package"]) != nil {
			defaultPaths = []string{workspace.manifestPath}
		} else {
			defaultPaths = paths
		}
	}

	targetDir := opts.targetDir
	if targetDir == "" {
		targetDir = os.Getenv("CARGO_TARGET_DIR")
	}
	if targetDir == "" {
		targetDir = filepath.Join(workspace.dir, "target")
	} else {
		targetDir = absolutePath(targetDir)
	}

	return MetadataOutput{
		Packages:                packages,
		WorkspaceMembers:        metadataIDs(paths, ids),
		WorkspaceDefaultMembers: metadataIDs(defaultPaths, ids),
		Resolve:                 nil,
		TargetDirectory:         targetDir,
		Version:                 1,
		WorkspaceRoot:           workspace.dir,
		Metadata:                mapValue(readToml(workspace.manifestPath)["workspace"])["metadata"],
	}
}

func metadataPackage(pkg *Package, doc map[string]any) MetadataPackage {
	table := mapValue(doc["package"])
	features := map[string][]string{}
	for name, values := range pkg.features {
		features[name] = append([]string(nil), values...)
	}
	features["default"] = append([]string(nil), pkg.defaultFeature...)

	result := MetadataPackage{
		Name: pkg.name, Version: pkg.version.string(), ID: metadataPackageID(pkg),
		License: nullableString(table["license"]), LicenseFile: nullablePath(pkg.dir, table["license-file"]),
		Description: nullableString(table["description"]), Source: nil,
		Targets: metadataTargets(pkg), Features: features, ManifestPath: pkg.manifestPath,
		Metadata: mapValue(table["metadata"]), Publish: metadataPublish(table["publish"]),
		Authors: stringsValue(table["authors"]), Categories: stringsValue(table["categories"]),
		Keywords: stringsValue(table["keywords"]), Readme: nullablePath(pkg.dir, table["readme"]),
		Repository: nullableString(table["repository"]), Homepage: nullableString(table["homepage"]),
		Documentation: nullableString(table["documentation"]), Edition: pkg.edition,
		Links: nullableString(table["links"]), DefaultRun: nullableString(table["default-run"]),
		RustVersion: nullableString(table["rust-version"]),
	}

	result.Dependencies = appendMetadataDependencies(result.Dependencies, pkg.dependencies.main, nil, nil)
	result.Dependencies = appendMetadataDependencies(result.Dependencies, pkg.dependencies.build, "build", nil)
	result.Dependencies = appendMetadataDependencies(result.Dependencies, pkg.dependencies.dev, "dev", nil)
	conditions := make([]string, 0, len(pkg.targetDeps))
	for condition := range pkg.targetDeps {
		conditions = append(conditions, condition)
	}
	sort.Strings(conditions)
	for _, condition := range conditions {
		deps := pkg.targetDeps[condition]
		result.Dependencies = appendMetadataDependencies(result.Dependencies, deps.main, nil, condition)
		result.Dependencies = appendMetadataDependencies(result.Dependencies, deps.build, "build", condition)
		result.Dependencies = appendMetadataDependencies(result.Dependencies, deps.dev, "dev", condition)
	}

	return result
}

func metadataTargets(pkg *Package) []MetadataTarget {
	result := make([]MetadataTarget, 0, len(pkg.targets))
	for _, target := range pkg.targets {
		crateTypes := append([]string(nil), target.crateTypes...)
		if len(crateTypes) == 0 {
			crateTypes = []string{"bin"}
		}
		result = append(result, MetadataTarget{
			Kind: []string{target.kind}, CrateTypes: crateTypes, Name: target.name,
			SrcPath: absoluteFrom(pkg.dir, target.path), Edition: target.edition,
			Doc: target.doc, Doctest: target.doctest, Test: target.test,
			RequiredFeatures: append([]string(nil), target.requiredFeatures...),
		})
	}
	return result
}

func appendMetadataDependencies(out []MetadataDependency, deps []*Dependency, kind, target any) []MetadataDependency {
	for _, dep := range deps {
		var rename any
		if dep.key != dep.name {
			rename = dep.key
		}
		var path any
		if dep.path != "" {
			path = dep.path
		}
		var source any
		if dep.git != "" {
			source = "git+" + dep.git
		} else if dep.path == "" {
			source = "registry+https://github.com/rust-lang/crates.io-index"
		}
		out = append(out, MetadataDependency{
			Name: dep.name, Source: source, Req: dep.version.string(), Kind: kind,
			Rename: rename, Optional: dep.optional, UsesDefaultFeatures: dep.defaultFeatures,
			Features: append([]string(nil), dep.features...), Target: target, Registry: nil, Path: path,
		})
	}
	return out
}

func metadataPackageID(pkg *Package) string {
	return "path+file://" + filepath.ToSlash(pkg.dir) + "#" + pkg.name + "@" + pkg.version.string()
}

func metadataIDs(paths []string, ids map[string]string) []string {
	result := make([]string, 0, len(paths))
	for _, path := range uniqueSortedPaths(paths) {
		if id := ids[path]; id != "" {
			result = append(result, id)
		}
	}
	return result
}

func uniqueSortedPaths(paths []string) []string {
	seen := map[string]bool{}
	result := make([]string, 0, len(paths))
	for _, path := range paths {
		path = absolutePath(path)
		if !seen[path] {
			seen[path] = true
			result = append(result, path)
		}
	}
	sort.Strings(result)
	return result
}

func nullableString(value any) any {
	if text := stringValue(value); text != "" {
		return text
	}
	return nil
}

func nullablePath(dir string, value any) any {
	if text := stringValue(value); text != "" {
		return absoluteFrom(dir, text)
	}
	return nil
}

func metadataPublish(value any) any {
	if value == nil {
		return nil
	}
	if enabled, ok := value.(bool); ok && !enabled {
		return []string{}
	}
	if registries := stringsValue(value); registries != nil {
		return registries
	}
	return nil
}
