package main

import (
	"bufio"
	"os/exec"
	"strings"
	"unicode"
)

type CfgSet struct {
	flags  map[string]bool
	values map[string]map[string]bool
}

type CfgParser struct {
	input string
	pos   int
}

func compilerCfg(compiler, target string) *CfgSet {
	args := []string{"-Z", "print-cfgs"}

	if target != "" {
		args = append([]string{"--target", target}, args...)
	}

	cmd := exec.Command(compiler, args...)
	output := throw2(cmd.Output())
	result := &CfgSet{flags: map[string]bool{}, values: map[string]map[string]bool{}}
	scanner := bufio.NewScanner(strings.NewReader(string(output)))

	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())

		line = strings.TrimPrefix(line, ">")

		if line == "" {
			continue
		}

		key, value, found := strings.Cut(line, "=")

		if !found {
			result.flags[key] = true

			continue
		}

		value = strings.Trim(value, "\"")

		if result.values[key] == nil {
			result.values[key] = map[string]bool{}
		}

		result.values[key][value] = true
	}

	throw(scanner.Err())

	return result
}

func (c *CfgSet) matches(condition string, features map[string]bool) bool {
	condition = strings.TrimSpace(condition)

	if !strings.HasPrefix(condition, "cfg(") {
		if values := c.values["target"]; values != nil {
			return values[condition]
		}

		return false
	}

	parser := &CfgParser{input: condition}

	parser.ident()
	parser.expect('(')

	result := parser.expression(c, features)

	parser.expect(')')

	return result
}

func (p *CfgParser) expression(cfg *CfgSet, features map[string]bool) bool {
	name := p.ident()

	p.space()

	if p.take('(') {
		var values []bool

		if !p.peek(')') {
			for {
				values = append(values, p.expression(cfg, features))

				if !p.take(',') {
					break
				}
			}
		}

		p.expect(')')

		switch name {
		case "not":
			if len(values) != 1 {
				throwFmt("cfg not() expects one argument")
			}

			return !values[0]
		case "all":
			for _, value := range values {
				if !value {
					return false
				}
			}

			return true
		case "any":
			for _, value := range values {
				if value {
					return true
				}
			}

			return false
		default:
			throwFmt("unknown cfg operator %q", name)
		}
	}

	if p.take('=') {
		value := p.quoted()

		if name == "feature" {
			return features[value]
		}

		return cfg.values[name][value]
	}

	return cfg.flags[name]
}

func (p *CfgParser) ident() string {
	p.space()

	start := p.pos

	for p.pos < len(p.input) {
		r := rune(p.input[p.pos])

		if !unicode.IsLetter(r) && !unicode.IsDigit(r) && r != '_' && r != '-' {
			break
		}

		p.pos++
	}

	if start == p.pos {
		throwFmt("expected cfg identifier at %q", p.input[p.pos:])
	}

	return p.input[start:p.pos]
}

func (p *CfgParser) quoted() string {
	p.space()
	p.expect('"')
	var result strings.Builder

	for p.pos < len(p.input) {
		char := p.input[p.pos]

		p.pos++

		if char == '"' {
			return result.String()
		}

		if char == '\\' && p.pos < len(p.input) {
			char = p.input[p.pos]
			p.pos++
		}

		result.WriteByte(char)
	}

	throwFmt("unterminated cfg string")

	return ""
}

func (p *CfgParser) take(want byte) bool {
	p.space()

	if p.pos < len(p.input) && p.input[p.pos] == want {
		p.pos++

		return true
	}

	return false
}

func (p *CfgParser) peek(want byte) bool {
	p.space()

	return p.pos < len(p.input) && p.input[p.pos] == want
}

func (p *CfgParser) expect(want byte) {
	if !p.take(want) {
		throwFmt("expected %q at %q", want, p.input[p.pos:])
	}
}

func (p *CfgParser) space() {
	for p.pos < len(p.input) && unicode.IsSpace(rune(p.input[p.pos])) {
		p.pos++
	}
}
