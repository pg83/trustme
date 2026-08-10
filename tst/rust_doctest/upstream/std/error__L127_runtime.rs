// Extracted from library/std/src/error.rs:127
#![feature(error_reporter)]
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
struct SuperErrorSideKick;
impl fmt::Display for SuperErrorSideKick {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "SuperErrorSideKick is here!")
    }
}
impl Error for SuperErrorSideKick {}
fn get_super_error() -> Result<(), SuperError> {
    Err(SuperError { source: SuperErrorSideKick })
}

fn main() -> Result<(), Report<SuperError>> {
    get_super_error()?;
    Ok(())
}
