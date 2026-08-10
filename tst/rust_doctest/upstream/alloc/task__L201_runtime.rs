// Extracted from library/alloc/src/task.rs:201
#![allow(unused)]
#![feature(local_waker)]
extern crate alloc;
fn main() {
    use std::task::{LocalWake, ContextBuilder, LocalWaker, Waker};
    use std::future::Future;
    use std::pin::Pin;
    use std::rc::Rc;
    use std::cell::RefCell;
    use std::collections::VecDeque;


    thread_local! {
        // A queue containing all tasks ready to do progress
        static RUN_QUEUE: RefCell<VecDeque<Rc<Task>>> = RefCell::default();
    }

    type BoxedFuture = Pin<Box<dyn Future<Output = ()>>>;

    struct Task(RefCell<BoxedFuture>);

    impl LocalWake for Task {
        fn wake(self: Rc<Self>) {
            RUN_QUEUE.with_borrow_mut(|queue| {
                queue.push_back(self)
            })
        }
    }

    fn spawn<F>(future: F)
    where
        F: Future<Output=()> + 'static + Send + Sync
    {
        let task = RefCell::new(Box::pin(future));
        RUN_QUEUE.with_borrow_mut(|queue| {
            queue.push_back(Rc::new(Task(task)));
        });
    }

    fn block_on<F>(future: F)
    where
        F: Future<Output=()> + 'static + Sync + Send
    {
        spawn(future);
        loop {
            let Some(task) = RUN_QUEUE.with_borrow_mut(|queue| queue.pop_front()) else {
                // we exit, since there are no more tasks remaining on the queue
                return;
            };

            // cast the Rc<Task> into a `LocalWaker`
            let local_waker: LocalWaker = task.clone().into();
            // Build the context using `ContextBuilder`
            let mut cx = ContextBuilder::from_waker(Waker::noop())
                .local_waker(&local_waker)
                .build();

            // Poll the task
            let _ = task.0
                .borrow_mut()
                .as_mut()
                .poll(&mut cx);
        }
    }

    block_on(async {
        println!("hello world");
    });
}
