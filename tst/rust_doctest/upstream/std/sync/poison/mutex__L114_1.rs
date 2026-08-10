// Extracted from library/std/src/sync/poison/mutex.rs:114
#![allow(unused)]
fn main() {
    use std::sync::{Arc, Mutex};
    use std::thread;

    const N: usize = 3;

    let data_mutex = Arc::new(Mutex::new(vec![1, 2, 3, 4]));
    let res_mutex = Arc::new(Mutex::new(0));

    let mut threads = Vec::with_capacity(N);
    (0..N).for_each(|_| {
        let data_mutex_clone = Arc::clone(&data_mutex);
        let res_mutex_clone = Arc::clone(&res_mutex);

        threads.push(thread::spawn(move || {
            // Here we use a block to limit the lifetime of the lock guard.
            let result = {
                let mut data = data_mutex_clone.lock().unwrap();
                // This is the result of some important and long-ish work.
                let result = data.iter().fold(0, |acc, x| acc + x * 2);
                data.push(result);
                result
                // The mutex guard gets dropped here, together with any other values
                // created in the critical section.
            };
            // The guard created here is a temporary dropped at the end of the statement, i.e.
            // the lock would not remain being held even if the thread did some additional work.
            *res_mutex_clone.lock().unwrap() += result;
        }));
    });

    let mut data = data_mutex.lock().unwrap();
    // This is the result of some important and long-ish work.
    let result = data.iter().fold(0, |acc, x| acc + x * 2);
    data.push(result);
    // We drop the `data` explicitly because it's not necessary anymore and the
    // thread still has work to do. This allows other threads to start working on
    // the data immediately, without waiting for the rest of the unrelated work
    // to be done here.
    //
    // It's even more important here than in the threads because we `.join` the
    // threads after that. If we had not dropped the mutex guard, a thread could
    // be waiting forever for it, causing a deadlock.
    // As in the threads, a block could have been used instead of calling the
    // `drop` function.
    drop(data);
    // Here the mutex guard is not assigned to a variable and so, even if the
    // scope does not end after this line, the mutex is still released: there is
    // no deadlock.
    *res_mutex.lock().unwrap() += result;

    threads.into_iter().for_each(|thread| {
        thread
            .join()
            .expect("The thread creating or execution failed !")
    });

    assert_eq!(*res_mutex.lock().unwrap(), 800);
}
