package main

import "sync"

type Version struct {
	major int
	minor int
	patch int
	pre   string
	build string
}

type VersionBound struct {
	op      string
	version Version
	parts   int
}

type VersionSpec struct {
	bounds []VersionBound
}

type Dependency struct {
	key             string
	name            string
	version         VersionSpec
	path            string
	git             string
	branch          string
	optional        bool
	public          bool
	defaultFeatures bool
	features        []string
	enabled         bool
	packageRef      *Package
}

type Dependencies struct {
	main  []*Dependency
	build []*Dependency
	dev   []*Dependency
}

type Target struct {
	kind             string
	name             string
	path             string
	edition          string
	crateTypes       []string
	test             bool
	doctest          bool
	bench            bool
	doc              bool
	procMacro        bool
	plugin           bool
	harness          bool
	requiredFeatures []string
}

type BuildScriptOutput struct {
	preBuild   []string
	linkSearch []string
	linkLib    []string
	cfg        []string
	flags      []string
	env        map[string]string
	downstream map[string]string
	rerun      []string
}

type Package struct {
	dir            string
	manifestPath   string
	name           string
	version        Version
	edition        string
	links          string
	buildScript    string
	dependencies   Dependencies
	targetDeps     map[string]Dependencies
	targets        []*Target
	features       map[string][]string
	defaultFeature []string
	activeFeatures map[string]bool
	buildOutput    BuildScriptOutput
	magic          bool

	featureMu sync.Mutex
}

type Workspace struct {
	dir            string
	manifestPath   string
	edition        string
	resolver       string
	members        []string
	defaultMembers []string
	exclude        []string
	dependencies   map[string]*Dependency
	patches        map[string]string
}

type Repository struct {
	workspace *Workspace
	vendorDir string
	byName    map[string][]*Package
	byPath    map[string]*Package
	patches   map[string]string
	locked    map[string]map[Version]bool
	overrides *ManifestOverrides
}

type ManifestOverride struct {
	deletes [][]string
	adds    map[string]any
}

type ManifestOverrides struct {
	entries map[string]*ManifestOverride
}

type BuildOptions struct {
	manifestPath     string
	targetDir        string
	vendorDir        string
	scriptOverrides  string
	manifestOverride string
	target           string
	profile          string
	jobs             int
	features         []string
	allFeatures      bool
	noDefault        bool
	libSearch        []string
	emitMmir         bool
	dryRun           bool
	verbose          int
	command          string
	noRun            bool
	testArgs         []string
	selectors        TargetSelectors
	packageName      string
	workspaceAll     bool
	excludePackages  []string
	pause            bool
}

type TargetSelectors struct {
	lib      bool
	bins     bool
	bin      []string
	tests    bool
	test     []string
	examples bool
	example  []string
	benches  bool
	bench    []string
}

type BuildContext struct {
	opts       BuildOptions
	repository *Repository
	root       *Package
	workspace  *Workspace
	compiler   string
	cfg        *CfgSet
	host       string
	target     string
	cross      bool
}
