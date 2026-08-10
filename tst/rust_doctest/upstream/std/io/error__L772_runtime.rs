// Extracted from library/std/src/io/error.rs:772
use std::io::{Error, ErrorKind};
use std::{error, fmt};
use std::fmt::Display;

#[derive(Debug)]
struct MyError {
    v: String,
}

impl MyError {
    fn new() -> MyError {
        MyError {
            v: "oh no!".to_string()
        }
    }

    fn change_message(&mut self, new_message: &str) {
        self.v = new_message.to_string();
    }
}

impl error::Error for MyError {}

impl Display for MyError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "MyError: {}", self.v)
    }
}

fn change_error(mut err: Error) -> Error {
    if let Some(inner_err) = err.get_mut() {
        inner_err.downcast_mut::<MyError>().unwrap().change_message("I've been changed!");
    }
    err
}

fn print_error(err: &Error) {
    if let Some(inner_err) = err.get_ref() {
        println!("Inner error: {inner_err}");
    } else {
        println!("No inner error");
    }
}

fn main() {
    // Will print "No inner error".
    print_error(&change_error(Error::last_os_error()));
    // Will print "Inner error: ...".
    print_error(&change_error(Error::new(ErrorKind::Other, MyError::new())));
}
