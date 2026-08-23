package main

import (
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"io/fs"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"syscall"
)

const taskCacheVersion = "cargo-cas-v2"

type TaskOutput struct {
	name       string
	tree       bool
	executable bool
}

type Task struct {
	key       string
	name      string
	kind      string
	deps      []*Task
	inputs    []string
	outputs   []TaskOutput
	signature []string
	action    func(*TaskContext)
	after     func(*TaskContext)
	state     *TaskState
}

type TaskState struct {
	uid      string
	manifest map[string]OutputEntry
}

type OutputEntry struct {
	Cas  string      `json:"cas,omitempty"`
	Link string      `json:"link,omitempty"`
	Mode fs.FileMode `json:"mode,omitempty"`
}

type TaskResult struct {
	task *Task
	err  *Exception
}

type TaskContext struct {
	executor *TaskExecutor
	task     *Task
	tmp      string
}

type TaskExecutor struct {
	root     string
	jobs     int
	dryRun   bool
	terminal bool
	outputMu sync.Mutex
	digestMu sync.Mutex
	digests  map[string]string
	done     int
	total    int
	toolHash string
}

func runTasks(tasks []*Task, jobs int, root string, dryRun bool) *TaskExecutor {
	if jobs < 1 {
		jobs = 1
	}

	info := throw2(os.Stderr.Stat())
	executor := &TaskExecutor{
		root:     root,
		jobs:     jobs,
		dryRun:   dryRun,
		terminal: info.Mode()&os.ModeCharDevice != 0,
		toolHash: fileDigest(throw2(os.Executable())),
		digests:  map[string]string{},
	}

	throw(os.MkdirAll(filepath.Join(root, "cas"), 0o755))
	throw(os.MkdirAll(filepath.Join(root, "uid"), 0o755))
	throw(os.MkdirAll(filepath.Join(root, "tmp"), 0o755))
	executor.run(tasks)

	return executor
}

func (ex *TaskExecutor) run(roots []*Task) {
	unique := uniqueTasks(roots)
	dependents := map[*Task][]*Task{}
	pending := map[*Task]int{}

	for _, task := range unique {
		uid := ex.taskUID(task)
		manifest := map[string]OutputEntry(nil)
		if !ex.dryRun {
			manifest = ex.loadManifest(ex.uidPath(uid))
		}
		task.state = &TaskState{uid: uid, manifest: manifest}
		if ex.dryRun || manifest == nil {
			ex.total++
		}

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
	results := make(chan TaskResult, ex.jobs)
	running := 0
	completed := 0
	failed := false
	var first *Exception

	for completed < len(unique) {
		for !failed && running < ex.jobs && len(ready) > 0 {
			task := ready[0]
			ready = ready[1:]
			running++

			go func() {
				err := try(func() {
					ex.execute(task)
				})
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

func (ex *TaskExecutor) execute(task *Task) {
	uid := task.state.uid
	manifestPath := ex.uidPath(uid)
	tmp := filepath.Join(ex.root, "tmp", uid)
	ctx := &TaskContext{executor: ex, task: task, tmp: tmp}

	if !ex.dryRun && task.state.manifest != nil {
		if task.after != nil {
			task.after(ctx)
		}
		return
	}

	lock := throw2(os.OpenFile(tmp+".lock", os.O_CREATE|os.O_RDWR, 0o644))

	defer func() {
		throw(lock.Close())
	}()

	throw(syscall.Flock(int(lock.Fd()), syscall.LOCK_EX))

	if !ex.dryRun {
		if manifest := ex.loadManifest(manifestPath); manifest != nil {
			task.state = &TaskState{uid: uid, manifest: manifest}

			if task.after != nil {
				task.after(ctx)
			}

			return
		}
	}

	throw(os.RemoveAll(tmp))
	throw(os.MkdirAll(filepath.Join(tmp, "out"), 0o755))

	if task.action != nil {
		task.action(ctx)
	}

	manifest := map[string]OutputEntry{}

	if !ex.dryRun {
		manifest = ex.storeOutputs(task, ctx)
		ex.writeManifest(manifestPath, manifest)
	}

	task.state = &TaskState{uid: uid, manifest: manifest}

	if task.after != nil {
		task.after(ctx)
	}

	throw(os.RemoveAll(tmp))
	ex.progress(task)
}

func (ex *TaskExecutor) taskUID(task *Task) string {
	hash := sha256.New()
	writeHashString(hash, taskCacheVersion)
	writeHashString(hash, ex.toolHash)
	writeHashString(hash, task.key)
	writeHashString(hash, task.kind)

	for _, value := range task.signature {
		writeHashString(hash, value)
	}

	for _, dep := range task.deps {
		if dep.state == nil {
			throwFmt("internal: dependency %s has no result", dep.name)
		}

		writeHashString(hash, dep.state.uid)
	}

	inputs := append([]string(nil), task.inputs...)
	sort.Strings(inputs)

	for _, input := range inputs {
		writeHashString(hash, input)
		writeHashString(hash, ex.digest(input))
	}

	for _, output := range task.outputs {
		writeHashString(hash, output.name)
		writeHashString(hash, fmt.Sprintf("%t:%t", output.tree, output.executable))
	}

	sum := hash.Sum(nil)

	return base64.RawURLEncoding.EncodeToString(sum[:16])
}

func writeHashString(out io.Writer, value string) {
	_, _ = fmt.Fprintf(out, "%d:", len(value))
	_, _ = io.WriteString(out, value)
}

func fileDigest(path string) string {
	hash := sha256.New()
	file := throw2(os.Open(path))
	_, err := io.Copy(hash, file)
	throw(file.Close())
	throw(err)

	return hex.EncodeToString(hash.Sum(nil))
}

func (ex *TaskExecutor) digest(path string) string {
	ex.digestMu.Lock()
	value := ex.digests[path]
	ex.digestMu.Unlock()

	if value != "" {
		return value
	}

	value = fileDigest(path)
	ex.digestMu.Lock()
	ex.digests[path] = value
	ex.digestMu.Unlock()

	return value
}

func (ex *TaskExecutor) storeOutputs(task *Task, ctx *TaskContext) map[string]OutputEntry {
	manifest := map[string]OutputEntry{}

	for index, output := range task.outputs {
		path := ctx.output(index)

		if output.tree {
			throw(filepath.WalkDir(path, func(entryPath string, entry fs.DirEntry, err error) error {
				if err != nil {
					return err
				}

				if entry.IsDir() {
					return nil
				}

				rel := throw2(filepath.Rel(path, entryPath))
				logical := filepath.ToSlash(filepath.Join(output.name, rel))
				if entry.Type()&os.ModeSymlink != 0 {
					manifest[logical] = OutputEntry{Link: throw2(os.Readlink(entryPath))}

					return nil
				}
				info := throw2(entry.Info())
				manifest[logical] = ex.storeFile(entryPath, info.Mode().Perm()&0o111 != 0)

				return nil
			}))

			continue
		}

		manifest[output.name] = ex.storeFile(path, output.executable)
	}

	return manifest
}

func (ex *TaskExecutor) storeFile(path string, executable bool) OutputEntry {
	file := throw2(os.Open(path))
	hash := sha256.New()
	_, err := io.Copy(hash, file)
	throw(file.Close())
	throw(err)
	value := hex.EncodeToString(hash.Sum(nil))
	destination := ex.casPath(value)
	mode := fs.FileMode(0o644)

	if executable {
		mode = 0o755
	}

	if _, err := os.Stat(destination); err != nil {
		throw(os.MkdirAll(filepath.Dir(destination), 0o755))
		tmp := throw2(os.CreateTemp(filepath.Dir(destination), ".cas-*"))
		source := throw2(os.Open(path))
		_, copyErr := io.Copy(tmp, source)
		throw(source.Close())
		throw(copyErr)
		throw(tmp.Chmod(mode))
		throw(tmp.Close())

		if err := os.Rename(tmp.Name(), destination); err != nil {
			_ = os.Remove(tmp.Name())

			if _, statErr := os.Stat(destination); statErr != nil {
				throw(err)
			}
		}
	}
	if executable {
		throw(os.Chmod(destination, mode))
	}

	return OutputEntry{Cas: value, Mode: mode}
}

func (ex *TaskExecutor) loadManifest(path string) map[string]OutputEntry {
	data, err := os.ReadFile(path)

	if err != nil {
		return nil
	}

	manifest := map[string]OutputEntry{}

	if json.Unmarshal(data, &manifest) != nil {
		return nil
	}

	for _, entry := range manifest {
		if entry.Link != "" {
			continue
		}
		if len(entry.Cas) != sha256.Size*2 {
			return nil
		}
		if _, err := os.Stat(ex.casPath(entry.Cas)); err != nil {
			return nil
		}
	}

	return manifest
}

func (ex *TaskExecutor) writeManifest(path string, manifest map[string]OutputEntry) {
	throw(os.MkdirAll(filepath.Dir(path), 0o755))
	data := throw2(json.Marshal(manifest))
	tmp := throw2(os.CreateTemp(filepath.Dir(path), ".uid-*"))
	_, err := tmp.Write(data)
	throw(err)
	throw(tmp.Close())
	throw(os.Rename(tmp.Name(), path))
}

func (ctx *TaskContext) output(index int) string {
	output := ctx.task.outputs[index]
	clean := filepath.Clean(output.name)

	if clean == "." || filepath.IsAbs(clean) || clean == ".." || strings.HasPrefix(clean, ".."+string(filepath.Separator)) {
		throwFmt("internal: unsafe task output %q", output.name)
	}

	path := filepath.Join(ctx.tmp, "out", clean)

	if output.tree {
		throw(os.MkdirAll(path, 0o755))
	} else {
		throw(os.MkdirAll(filepath.Dir(path), 0o755))
	}

	return path
}

func (ctx *TaskContext) outputBase(name string) string {
	clean := filepath.Clean(name)

	if filepath.IsAbs(clean) || clean == ".." || strings.HasPrefix(clean, ".."+string(filepath.Separator)) {
		throwFmt("internal: unsafe task output base %q", name)
	}

	path := filepath.Join(ctx.tmp, "out", clean)
	throw(os.MkdirAll(filepath.Dir(path), 0o755))

	return path
}

func (ctx *TaskContext) file(task *Task, index int) string {
	if task.state == nil {
		throwFmt("internal: task %s has no result", task.name)
	}

	name := task.outputs[index].name

	if ctx.executor.dryRun {
		return filepath.Join(ctx.executor.root, "cas", "dry", task.state.uid, name)
	}

	entry, ok := task.state.manifest[name]

	if !ok {
		throwFmt("internal: task %s has no file output %s", task.name, name)
	}

	return ctx.executor.casPath(entry.Cas)
}

func (ctx *TaskContext) tree(task *Task, index int) string {
	if task.state == nil {
		throwFmt("internal: task %s has no result", task.name)
	}

	root := filepath.Join(ctx.tmp, "in", task.state.uid, fmt.Sprintf("%d", index))
	throw(os.MkdirAll(root, 0o755))
	prefix := task.outputs[index].name + "/"

	for logical, entry := range task.state.manifest {
		if !strings.HasPrefix(logical, prefix) {
			continue
		}

		rel := strings.TrimPrefix(logical, prefix)
		clean := filepath.Clean(filepath.FromSlash(rel))
		if clean == "." || filepath.IsAbs(clean) || clean == ".." || strings.HasPrefix(clean, ".."+string(filepath.Separator)) {
			throwFmt("malformed tree entry %q for task %s", logical, task.name)
		}
		destination := filepath.Join(root, clean)
		throw(os.MkdirAll(filepath.Dir(destination), 0o755))

		if _, err := os.Stat(destination); err == nil {
			continue
		}

		if entry.Link != "" {
			throw(os.Symlink(entry.Link, destination))

			continue
		}

		source := ctx.executor.casPath(entry.Cas)
		if err := os.Link(source, destination); err != nil {
			throw(os.Symlink(source, destination))
		}
	}

	return root
}

func (ex *TaskExecutor) install(task *Task, index int, destination string) {
	ctx := &TaskContext{executor: ex}
	source := ctx.file(task, index)
	entry := task.state.manifest[task.outputs[index].name]
	throw(os.MkdirAll(filepath.Dir(destination), 0o755))
	_ = os.Remove(destination)
	sourceInfo := throw2(os.Stat(source))

	if sourceInfo.Mode().Perm() == entry.Mode.Perm() {
		if err := os.Link(source, destination); err == nil {
			return
		}
	}

	{
		input := throw2(os.Open(source))
		output := throw2(os.OpenFile(destination, os.O_CREATE|os.O_TRUNC|os.O_WRONLY, entry.Mode))
		_, copyErr := io.Copy(output, input)
		throw(input.Close())
		throw(output.Close())
		throw(copyErr)
	}

	throw(os.Chmod(destination, entry.Mode))
}

func (ex *TaskExecutor) progress(task *Task) {
	ex.outputMu.Lock()
	defer ex.outputMu.Unlock()
	ex.done++
	kind := task.kind

	if kind == "" {
		kind = "RUN"
	}

	line := fmt.Sprintf("[%s] {%d/%d} %s", kind, ex.done, ex.total, task.outputs[0].name)

	if ex.terminal {
		fmt.Fprintf(os.Stderr, "\x1b[2K\r%s", line)

		if ex.done == ex.total {
			fmt.Fprintln(os.Stderr)
		}
	} else {
		fmt.Fprintln(os.Stderr, line)
	}
}

func (ex *TaskExecutor) casPath(hash string) string {
	return filepath.Join(ex.root, "cas", hash[:2], hash)
}

func (ex *TaskExecutor) uidPath(uid string) string {
	return filepath.Join(ex.root, "uid", uid[:1], uid)
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
		return tasks[i].key < tasks[j].key
	})
}
