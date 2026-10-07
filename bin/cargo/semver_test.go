package main

import "testing"

func TestVersionSpecs(t *testing.T) {
	tests := []struct {
		spec    string
		version string
		want    bool
	}{
		{"1", "1.9.0", true},
		{"1", "2.0.0", false},
		{"0", "0.2.189", true},
		{"0", "1.0.0", false},
		{"0.0", "0.0.9", true},
		{"0.0", "0.1.0", false},
		{"^0.0.0", "0.0.1", false},
		{"^0.2.3", "0.2.9", true},
		{"^0.2.3", "0.3.0", false},
		{"~1.2", "1.2.99", true},
		{"~1.2", "1.3.0", false},
		{">=1.2, <2", "1.8.0", true},
		{">=1.2, <2", "2.0.0", false},
		{"1.2.*", "1.2.7", true},
		{"1.2.*", "1.3.0", false},
		// num_enum_derive asks for proc-macro-crate ">= 1, <= 3" and arti locks
		// 3.5.0: a part a comparator leaves out matches any value
		// (`matches_exact`, `matches_greater`, `matches_less`, semver eval.rs).
		{">= 1, <= 3", "3.5.0", true},
		{"<= 3", "4.0.0", false},
		{"<=1.2", "1.2.9", true},
		{"<=1.2", "1.3.0", false},
		{">1", "1.5.0", false},
		{">1", "2.0.0", true},
		{">1.2", "1.2.9", false},
		{">1.2", "1.3.0", true},
		{"<1", "1.0.0", false},
		{"<1.2", "1.1.9", true},
		{"<1.2", "1.2.0", false},
		{">=1.2", "1.2.0", true},
	}

	for _, test := range tests {
		if got := parseVersionSpec(test.spec).accepts(parseVersion(test.version)); got != test.want {
			t.Errorf("%s accepts %s = %v, want %v", test.spec, test.version, got, test.want)
		}
	}
}
