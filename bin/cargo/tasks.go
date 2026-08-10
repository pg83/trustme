package main

import (
	"fmt"
	"os"
	"sort"
	"sync"
)

type Task struct {
	name    string
	verb    string
	deps    []*Task
	inputs  []string
	outputs []string
	action  func()
}

type TaskResult struct {
	task *Task
	err  *Exception
}

func runTasks(tasks []*Task, jobs int) {
	if jobs < 1 {
		jobs = 1
	}

	unique := uniqueTasks(tasks)
	dependents := map[*Task][]*Task{}
	pending := map[*Task]int{}

	for _, task := range unique {
		pending[task] = len(task.deps)

		for _, dep := range task.deps {
			dependents[dep] = append(dependents[dep], task)
		}
	}

	ready := make([]*Task, 0)

	for _, task := range unique {
		if pending[task] == 0 {
			ready = append(ready, task)
		}
	}

	sortTasks(ready)

	results := make(chan TaskResult, jobs)
	running := 0
	completed := 0
	failed := false

	var first *Exception
	var outputMu sync.Mutex

	for completed < len(unique) {
		for !failed && running < jobs && len(ready) > 0 {
			task := ready[0]

			ready = ready[1:]
			running++

			go func() {
				err := try(func() {
					if taskUpToDate(task) {
						return
					}

					outputMu.Lock()
					fmt.Fprintf(os.Stderr, "   %s %s\n", task.verb, task.name)
					outputMu.Unlock()
					task.action()
				})

				if err != nil {
					for _, output := range task.outputs {
						_ = os.Remove(output)
					}
				}

				results <- TaskResult{task: task, err: err}
			}()
		}

		if running == 0 {
			if failed {
				break
			}

			throwFmt("build graph deadlock: %d of %d tasks completed", completed, len(unique))
		}

		result := <-results

		running--
		completed++

		if result.err != nil {
			failed = true

			if first == nil {
				first = result.err
			}

			continue
		}

		for _, next := range dependents[result.task] {
			pending[next]--

			if pending[next] == 0 {
				ready = append(ready, next)
			}
		}

		sortTasks(ready)
	}

	for running > 0 {
		result := <-results

		running--

		if result.err != nil && first == nil {
			first = result.err
		}
	}

	if first != nil {
		first.throw()
	}
}

func uniqueTasks(roots []*Task) []*Task {
	seen := map[*Task]bool{}

	var result []*Task
	var visit func(*Task)
	visit = func(task *Task) {
		if task == nil || seen[task] {
			return
		}

		seen[task] = true

		for _, dep := range task.deps {
			visit(dep)
		}

		result = append(result, task)
	}

	for _, task := range roots {
		visit(task)
	}

	return result
}

func sortTasks(tasks []*Task) {
	sort.Slice(tasks, func(i, j int) bool {
		return tasks[i].name < tasks[j].name
	})
}

func taskUpToDate(task *Task) bool {
	if len(task.outputs) == 0 {
		return false
	}

	oldestOutput := int64(1<<63 - 1)

	for _, output := range task.outputs {
		info, err := os.Stat(output)

		if err != nil {
			return false
		}

		if stamp := info.ModTime().UnixNano(); stamp < oldestOutput {
			oldestOutput = stamp
		}
	}

	inputs := append([]string(nil), task.inputs...)

	for _, output := range task.outputs {
		inputs = append(inputs, depfileInputs(output+".d")...)
	}

	for _, dep := range task.deps {
		inputs = append(inputs, dep.outputs...)
	}

	for _, input := range inputs {
		info, err := os.Stat(input)

		if err == nil && info.ModTime().UnixNano() > oldestOutput {
			return false
		}
	}

	return true
}

func depfileInputs(path string) []string {
	data, err := os.ReadFile(path)

	if err != nil {
		return nil
	}

	var tokens []string
	var token []byte

	escaped := false
	afterColon := false

	flush := func() {
		if len(token) == 0 {
			return
		}

		value := string(token)

		token = token[:0]

		if value == ":" {
			afterColon = true

			return
		}

		if afterColon {
			tokens = append(tokens, value)
		}
	}

	for _, char := range data {
		if escaped {
			if char != '\n' {
				token = append(token, char)
			}

			escaped = false

			continue
		}

		if char == '\\' {
			escaped = true

			continue
		}

		if char == ':' {
			flush()
			token = append(token, char)
			flush()

			continue
		}

		if char == ' ' || char == '\t' || char == '\r' || char == '\n' {
			flush()

			continue
		}

		token = append(token, char)
	}

	flush()

	return tokens
}
