//@ aux-build: w401_old/test_cov99_w401_parent.rs
//@ aux-output-dir: w401_old/test_cov99_w401_parent.rs=w401-old
//@ aux-build: test_cov99_w401_previous.rs
//@ aux-build: w401_new/test_cov99_w401_parent.rs
//@ aux-output-dir: w401_new/test_cov99_w401_parent.rs=w401-new
//@ compile-only
//@ compile-flags: -Zmir-opt-level=3 -Zdump-mir -Zstop-after=mir

extern crate test_cov99_w401_parent as modern;
extern crate test_cov99_w401_previous as previous;

trait Both: previous::OldParent<1> + modern::Parent<1, 0> {}

fn dispatch(value: &dyn Both) -> u32 {
    previous::read(value)
}

fn main() {}
