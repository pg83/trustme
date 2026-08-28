//@ crate-type: lib
//@ compile-flags: -Znext-solver=globally
//@ edition: 2024

#![no_std]

// The `Break` arm introduced by `?` has type `!`.  It must not become the
// common match type while the `Continue` arm is waiting for the solver's
// concrete `Try::Output` response.  In particular, the later comparison must
// not infer both still-open operands as `!` from the real `PartialOrd for !`
// implementation.
struct Buffer {
    len: usize,
}

impl Buffer {
    fn capacity(&self) -> usize {
        self.len
    }

    fn reserve(&self, additional: usize) -> Result<(), ()> {
        let new_capacity = self.len.checked_add(additional).ok_or(())?;
        let old_capacity = self.capacity();
        if new_capacity > old_capacity {
            return Err(());
        }
        Ok(())
    }
}
