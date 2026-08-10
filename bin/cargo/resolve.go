package main

import (
	"sort"
	"strings"
)

func resolveGraph(context *BuildContext) []*Package {
	root := context.root

	if context.opts.allFeatures {
		for feature := range root.features {
			activateFeature(root, feature)
		}
	} else {
		if !context.opts.noDefault {
			for _, feature := range root.defaultFeature {
				activateFeature(root, feature)
			}
		}

		for _, feature := range context.opts.features {
			activateFeature(root, feature)
		}
	}

	queue := []*Package{root}
	queued := map[*Package]bool{root: true}
	visitedRevision := map[*Package]string{}

	var packages []*Package

	for len(queue) > 0 {
		pkg := queue[0]

		queue = queue[1:]
		queued[pkg] = false
		expandFeatures(pkg)

		revision := featureRevision(pkg)
		previous, seen := visitedRevision[pkg]

		if seen && previous == revision {
			continue
		}

		if !seen {
			packages = append(packages, pkg)
		}

		visitedRevision[pkg] = revision

		includeDev := pkg == root && context.opts.command == "test"

		for _, dep := range enabledDependencies(context, pkg, includeDev) {
			child := context.repository.resolve(dep, pkg)
			changed := requestFeatures(child, dep.features, dep.defaultFeatures)

			if changed || visitedRevision[child] == "" {
				if !queued[child] {
					queue = append(queue, child)
					queued[child] = true
				}
			}
		}
	}

	sort.Slice(packages, func(i, j int) bool {
		if packages[i].name != packages[j].name {
			return packages[i].name < packages[j].name
		}

		return compareVersion(packages[i].version, packages[j].version) < 0
	})

	return packages
}

func activateFeature(pkg *Package, feature string) bool {
	feature = strings.TrimSpace(feature)

	if feature == "" {
		return false
	}

	pkg.featureMu.Lock()

	defer pkg.featureMu.Unlock()

	if pkg.activeFeatures[feature] {
		return false
	}

	pkg.activeFeatures[feature] = true

	return true
}

func requestFeatures(pkg *Package, features []string, defaults bool) bool {
	changed := false

	if defaults {
		for _, feature := range pkg.defaultFeature {
			changed = activateFeature(pkg, feature) || changed
		}
	}

	for _, feature := range features {
		changed = activateFeature(pkg, feature) || changed
	}

	return changed
}

func expandFeatures(pkg *Package) {
	for {
		changed := false
		features := sortedFeatureKeys(pkg.activeFeatures)

		for _, feature := range features {
			if strings.HasPrefix(feature, "dep:") {
				changed = enableDependency(pkg, strings.TrimPrefix(feature, "dep:")) || changed

				continue
			}

			if depName, depFeature, hasSlash := strings.Cut(feature, "/"); hasSlash {
				changed = requestDependencyFeature(pkg, depName, depFeature) || changed

				continue
			}

			for _, sub := range pkg.features[feature] {
				depName, depFeature, hasSlash := strings.Cut(sub, "/")

				if hasSlash {
					changed = requestDependencyFeature(pkg, depName, depFeature) || changed

					continue
				}

				changed = activateFeature(pkg, sub) || changed
			}
		}

		if !changed {
			return
		}
	}
}

func requestDependencyFeature(pkg *Package, depName, depFeature string) bool {
	weak := strings.HasSuffix(depName, "?")

	depName = strings.TrimSuffix(depName, "?")

	dep := findDependency(pkg, depName)

	if dep == nil {
		return false
	}

	changed := false

	if !weak {
		changed = enableDependency(pkg, depName)
	}

	if !contains(dep.features, depFeature) {
		dep.features = append(dep.features, depFeature)
		changed = true
	}

	return changed
}

func enableDependency(pkg *Package, name string) bool {
	dep := findDependency(pkg, name)

	if dep == nil || dep.enabled {
		return false
	}

	dep.enabled = true

	return true
}

func findDependency(pkg *Package, name string) *Dependency {
	for _, dep := range allDependencies(pkg) {
		if dep.key == name {
			return dep
		}
	}

	return nil
}

func enabledDependencies(context *BuildContext, pkg *Package, includeDev bool) []*Dependency {
	deps := append([]*Dependency{}, pkg.dependencies.main...)

	_, buildScriptOverridden := buildScriptOverride(context, pkg)
	if pkg.buildScript != "" && !buildScriptOverridden {
		deps = append(deps, pkg.dependencies.build...)
	}

	if includeDev {
		deps = append(deps, pkg.dependencies.dev...)
	}

	for condition, group := range pkg.targetDeps {
		matches := condition == context.target

		if strings.HasPrefix(condition, "cfg(") {
			matches = context.cfg.matches(condition, pkg.activeFeatures)
		}

		if !matches {
			continue
		}

		deps = append(deps, group.main...)

		if pkg.buildScript != "" && !buildScriptOverridden {
			deps = append(deps, group.build...)
		}

		if includeDev {
			deps = append(deps, group.dev...)
		}
	}

	result := deps[:0]

	for _, dep := range deps {
		if !dep.optional || dep.enabled {
			result = append(result, dep)
		}
	}

	return result
}

func featureRevision(pkg *Package) string {
	return strings.Join(sortedFeatureKeys(pkg.activeFeatures), "\x00")
}

func sortedFeatureKeys(features map[string]bool) []string {
	result := make([]string, 0, len(features))

	for feature, enabled := range features {
		if enabled {
			result = append(result, feature)
		}
	}

	sort.Strings(result)

	return result
}

func contains(values []string, want string) bool {
	for _, value := range values {
		if value == want {
			return true
		}
	}

	return false
}
