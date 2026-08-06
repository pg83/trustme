// Extracted from library/std/src/error.rs:364
#![allow(unused)]
#![feature(error_reporter)]
#![feature(error_generic_member_access)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::error::Error;
        use std::fmt;
        use std::error::Request;
        use std::error::Report;
        use std::backtrace::Backtrace;
        
        #[derive(Debug)]
        struct SuperError {
            source: SuperErrorSideKick,
        }
        impl fmt::Display for SuperError {
            fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                write!(f, "SuperError is here!")
            }
        }
        impl Error for SuperError {
            fn source(&self) -> Option<&(dyn Error + 'static)> {
                Some(&self.source)
            }
        }
        #[derive(Debug)]
        struct SuperErrorSideKick {
            backtrace: Backtrace,
        }
        
        impl SuperErrorSideKick {
            fn new() -> SuperErrorSideKick {
                SuperErrorSideKick { backtrace: Backtrace::force_capture() }
            }
        }
        
        impl Error for SuperErrorSideKick {
            fn provide<'a>(&'a self, request: &mut Request<'a>) {
                request.provide_ref::<Backtrace>(&self.backtrace);
            }
        }
        
        // The rest of the example is unchanged ...
        impl fmt::Display for SuperErrorSideKick {
            fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                write!(f, "SuperErrorSideKick is here!")
            }
        }
        
        let source = SuperErrorSideKick::new();
        let error = SuperError { source };
        let report = Report::new(error).pretty(true).show_backtrace(true);
        eprintln!("Error: {report:?}");
        Ok(())
    }
    doctest().unwrap();
}
