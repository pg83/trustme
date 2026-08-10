// Extracted from library/core/src/hint.rs:562
#![allow(unused)]
#![feature(hint_must_use)]
fn main() {
      
      struct Error;
      
      macro_rules! make_error {
          ($($args:expr),*) => {
              core::hint::must_use({
                  // If `let` isn't used, then `f()` produces a non-Send future.
                  let error = make_error(core::format_args!($($args),*));
                  error
              })
          };
      }
      
      fn make_error(args: core::fmt::Arguments<'_>) -> Error {
          Error
      }
      
      async fn f() {
          // Using `let` inside the make_error expansion causes temporaries like
          // `unsync()` to drop at the semicolon of that `let` statement, which
          // is prior to the await point. They would otherwise stay around until
          // the semicolon on *this* statement, which is after the await point,
          // and the enclosing Future would not implement Send.
          log(make_error!("look: {:p}", unsync())).await;
      }

      async fn log(error: Error) {/* ... */}

      // Returns something without a Sync impl.
      fn unsync() -> *const () {
          0 as *const ()
      }
      
      fn test() {
          fn assert_send(_: impl Send) {}
          assert_send(f());
      }
}
