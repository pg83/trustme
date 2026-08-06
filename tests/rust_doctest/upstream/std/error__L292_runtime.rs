// Extracted from library/std/src/error.rs:292
#![allow(unused)]
#![feature(error_reporter)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::error::Report;
        use std::error::Error;
        use std::fmt;
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
            source: SuperErrorSideKickSideKick,
        }
        impl fmt::Display for SuperErrorSideKick {
            fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                write!(f, "SuperErrorSideKick is here!")
            }
        }
        impl Error for SuperErrorSideKick {
            fn source(&self) -> Option<&(dyn Error + 'static)> {
                Some(&self.source)
            }
        }
        #[derive(Debug)]
        struct SuperErrorSideKickSideKick;
        impl fmt::Display for SuperErrorSideKickSideKick {
            fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                write!(f, "SuperErrorSideKickSideKick is here!")
            }
        }
        impl Error for SuperErrorSideKickSideKick { }
        
        let source = SuperErrorSideKickSideKick;
        let source = SuperErrorSideKick { source };
        let error = SuperError { source };
        let report = Report::new(error).pretty(true);
        eprintln!("Error: {report:?}");
        Ok(())
    }
    doctest().unwrap();
}
