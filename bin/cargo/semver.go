package main

import (
	"fmt"
	"strconv"
	"strings"
)

func parseVersion(input string) Version {
	input = strings.TrimSpace(input)

	version := Version{}

	if plus := strings.IndexByte(input, '+'); plus >= 0 {
		version.build = input[plus+1:]
		input = input[:plus]
	}

	if dash := strings.IndexByte(input, '-'); dash >= 0 {
		version.pre = input[dash+1:]
		input = input[:dash]
	}

	parts := strings.Split(input, ".")

	if len(parts) == 0 || len(parts) > 3 {
		throwFmt("invalid version %q", input)
	}

	values := []*int{&version.major, &version.minor, &version.patch}

	for i, part := range parts {
		if part == "" {
			throwFmt("invalid version %q", input)
		}

		*values[i] = throw2(strconv.Atoi(part))
	}

	return version
}

func (v Version) string() string {
	value := fmt.Sprintf("%d.%d.%d", v.major, v.minor, v.patch)

	if v.pre != "" {
		value += "-" + v.pre
	}

	if v.build != "" {
		value += "+" + v.build
	}

	return value
}

func compareVersion(a, b Version) int {
	av := []int{a.major, a.minor, a.patch}
	bv := []int{b.major, b.minor, b.patch}

	for i := range av {
		if av[i] < bv[i] {
			return -1
		}

		if av[i] > bv[i] {
			return 1
		}
	}

	if a.pre == b.pre {
		return 0
	}

	if a.pre == "" {
		return 1
	}

	if b.pre == "" {
		return -1
	}

	return strings.Compare(a.pre, b.pre)
}

func parseVersionSpec(input string) VersionSpec {
	input = strings.TrimSpace(input)

	if input == "" || input == "*" {
		return VersionSpec{}
	}

	var spec VersionSpec

	for _, raw := range strings.Split(input, ",") {
		part := strings.TrimSpace(raw)
		op := "^"

		for _, candidate := range []string{"<=", ">=", "=", "<", ">", "~", "^"} {
			if strings.HasPrefix(part, candidate) {
				op = candidate
				part = strings.TrimSpace(strings.TrimPrefix(part, candidate))

				break
			}
		}

		if part == "*" {
			continue
		}

		parts := strings.Split(part, ".")
		wildcard := false

		for i, item := range parts {
			if item == "*" || strings.EqualFold(item, "x") {
				parts = parts[:i]
				wildcard = true

				break
			}
		}

		if len(parts) == 0 {
			continue
		}

		version := parseVersion(strings.Join(parts, "."))

		if wildcard {
			spec.bounds = append(spec.bounds, VersionBound{op: "wildcard", version: version, parts: len(parts)})
		} else {
			spec.bounds = append(spec.bounds, VersionBound{op: op, version: version, parts: len(parts)})
		}
	}

	return spec
}

func (s VersionSpec) accepts(version Version) bool {
	for _, bound := range s.bounds {
		cmp := compareVersion(version, bound.version)

		switch bound.op {
		case "=":
			if bound.parts == 1 && version.major != bound.version.major ||
				bound.parts == 2 && (version.major != bound.version.major || version.minor != bound.version.minor) ||
				bound.parts == 3 && cmp != 0 {
				return false
			}
		case ">":
			if cmp <= 0 {
				return false
			}
		case ">=":
			if cmp < 0 {
				return false
			}
		case "<":
			if cmp >= 0 {
				return false
			}
		case "<=":
			if cmp > 0 {
				return false
			}
		case "wildcard":
			if version.major != bound.version.major || bound.parts == 2 && version.minor != bound.version.minor {
				return false
			}
		case "~":
			if cmp < 0 || version.major != bound.version.major || bound.parts > 1 && version.minor != bound.version.minor {
				return false
			}
		case "^":
			if cmp < 0 || !compatible(bound.version, version, bound.parts) {
				return false
			}
		}
	}

	return true
}

func compatible(base, version Version, parts int) bool {
	if parts == 1 {
		return base.major == version.major
	}

	if base.major != 0 {
		return base.major == version.major
	}

	if parts == 2 || base.minor != 0 {
		return version.major == 0 && base.minor == version.minor
	}

	return version.major == 0 && version.minor == 0 && base.patch == version.patch
}
