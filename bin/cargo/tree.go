package main

import (
	"fmt"
	"io"
	"os"
	"sort"
	"strings"
)

// `cargo tree` prints the dependency graph the build would use. Only what
// rustix's tests ask of it is modelled: normal edges (`--edges=normal`, the
// default kinds of a plain build), the graph for the host, and `--invert=SPEC`
// printing the packages that depend on SPEC.
func cmdTree(args []string) {
	var invert []string
	var rest []string

	for i := 0; i < len(args); i++ {
		arg := args[i]
		value := func() string {
			if i+1 >= len(args) {
				throwFmt("option %s requires a value", arg)
			}
			i++

			return args[i]
		}

		name, inline, hasInline := strings.Cut(arg, "=")
		take := func() string {
			if hasInline {
				return inline
			}

			return value()
		}

		switch name {
		case "--invert", "-i":
			invert = append(invert, take())
		case "--edges", "-e":
			if edges := take(); edges != "normal" && edges != "no-dev" && edges != "no-build" {
				throwFmt("cargo tree --edges=%s is not supported", edges)
			}
		case "--prefix", "--charset", "--format", "--depth":
			_ = take()
		case "--no-dedupe", "--duplicates", "-d":
		default:
			rest = append(rest, arg)
		}
	}

	writeTree(os.Stdout, parseBuildOptions("build", rest), invert)
}

func writeTree(out io.Writer, opts BuildOptions, invert []string) {
	manifestPath := opts.manifestPath

	if manifestPath == "" {
		manifestPath = "Cargo.toml"
	}

	manifestPath = absolutePath(manifestPath)
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
		opts: opts, repository: repository, root: root, workspace: workspace,
		compiler: compiler, host: host, target: target,
		cross: opts.target != "" && opts.target != host,
	}
	context.rustflags = targetRustflags()
	context.cfg = compilerCfg(compiler, opts.target, context.rustflags)

	resolveGraph(context)
	printTree(out, context, root, invert)
}

func printTree(out io.Writer, context *BuildContext, root *Package, invert []string) {
	dependencies := map[*Package][]*Package{}
	dependents := map[*Package][]*Package{}
	reached := map[*Package]bool{root: true}
	declared := map[string]bool{}
	queue := []*Package{root}

	for len(queue) > 0 {
		pkg := queue[0]
		queue = queue[1:]

		for _, dep := range allDependencies(pkg) {
			declared[dep.name] = true
		}

		for _, dep := range normalDependencies(context, pkg) {
			child := context.repository.resolve(dep, pkg)
			dependencies[pkg] = append(dependencies[pkg], child)
			dependents[child] = append(dependents[child], pkg)

			if !reached[child] {
				reached[child] = true
				queue = append(queue, child)
			}
		}
	}

	edges := dependencies
	roots := []*Package{root}

	if len(invert) > 0 {
		edges = dependents
		roots = nil

		for _, spec := range invert {
			name, version, _ := strings.Cut(spec, "@")
			matched := false

			for pkg := range reached {
				if pkg.name == name && (version == "" || pkg.version.string() == version) {
					roots = append(roots, pkg)
					matched = true
				}
			}

			if !matched && !declared[name] {
				throwFmt("package ID specification `%s` did not match any packages", spec)
			}
		}
	}

	sortPackages(roots)
	printed := map[*Package]bool{}

	for i, pkg := range roots {
		if i > 0 {
			fmt.Fprintln(out)
		}

		printTreeNode(out, context, pkg, edges, printed, "", "")
	}
}

func printTreeNode(out io.Writer, context *BuildContext, pkg *Package, edges map[*Package][]*Package, printed map[*Package]bool, lead, childLead string) {
	label := pkg.name + " v" + pkg.version.string()

	if !context.repository.isVendored(pkg) && pkg.dir != "" {
		label += " (" + pkg.dir + ")"
	}

	children := uniquePackages(edges[pkg])

	if printed[pkg] && len(children) > 0 {
		fmt.Fprintln(out, lead+label+" (*)")

		return
	}

	fmt.Fprintln(out, lead+label)
	printed[pkg] = true

	for i, child := range children {
		if i == len(children)-1 {
			printTreeNode(out, context, child, edges, printed, childLead+"└── ", childLead+"    ")
		} else {
			printTreeNode(out, context, child, edges, printed, childLead+"├── ", childLead+"│   ")
		}
	}
}

func uniquePackages(packages []*Package) []*Package {
	seen := map[*Package]bool{}
	result := []*Package{}

	for _, pkg := range packages {
		if !seen[pkg] {
			seen[pkg] = true
			result = append(result, pkg)
		}
	}

	sortPackages(result)

	return result
}

func sortPackages(packages []*Package) {
	sort.Slice(packages, func(i, j int) bool {
		if packages[i].name != packages[j].name {
			return packages[i].name < packages[j].name
		}

		return compareVersion(packages[i].version, packages[j].version) < 0
	})
}
