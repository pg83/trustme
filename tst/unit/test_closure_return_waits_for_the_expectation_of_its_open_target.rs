/* std's `add_spawn_hook`: `hook: Box::new(move |thread| Box::new(hook(thread)))` into a
   `Box<dyn Fn(&Thread) -> Box<dyn FnOnce() + Send> + Send + Sync>` field - the closure is
   coerced into `Box::new`'s open parameter, and its return `Box<dyn FnOnce() + Send>`
   comes from the unsizing once that parameter is the closure; the body's `Box<G>`
   unsizes into it rather than deciding it. */
use std::sync::Arc;

struct Thread;

struct SpawnHook {
    hook: Box<dyn Fn(&Thread) -> Box<dyn FnOnce() + Send> + Send + Sync>,
    next: Option<Arc<SpawnHook>>,
}

fn add_spawn_hook<F, G>(hook: F) -> Arc<SpawnHook>
where
    F: 'static + Send + Sync + Fn(&Thread) -> G,
    G: 'static + Send + FnOnce(),
{
    let next = None;
    Arc::new(SpawnHook {
        hook: Box::new(move |thread| Box::new(hook(thread))),
        next,
    })
}

fn main() {
    let hook = add_spawn_hook(|_| || {});
    (hook.hook)(&Thread)();
    assert!(hook.next.is_none());
}
