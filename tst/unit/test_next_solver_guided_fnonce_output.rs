//@ check-pass

use std::panic::{catch_unwind, AssertUnwindSafe};

struct Bencher;

impl Bencher {
    fn bench<F>(&mut self, mut f: F) -> Result<Option<u64>, String>
    where
        F: FnMut(&mut Bencher) -> Result<(), String>,
    {
        f(self)?;
        Ok(Some(0))
    }
}

fn benchmark<F>(f: F)
where
    F: FnMut(&mut Bencher) -> Result<(), String>,
{
    let mut bencher = Bencher;
    let result = catch_unwind(AssertUnwindSafe(|| bencher.bench(f)));
    match result {
        Ok(Ok(Some(_))) => {}
        Ok(Ok(None)) => {}
        Ok(Err(_)) => {}
        Err(_) => {}
    }
}

fn main() {
    benchmark(|_| Ok(()));
}
