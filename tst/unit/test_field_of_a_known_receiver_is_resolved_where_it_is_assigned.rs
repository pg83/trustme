// A field of a receiver whose type is known is resolved where it is met (rustc
// `check_field`), so the value assigned to it is checked expecting the field's type:
// `state.0 = Some(Box::new(new_child_state.unwrap()))` with `state.0: Option<Box<dyn Any +
// Send>>` hands `Box::new` the expected `Box<dyn Any + Send>`, boxing the sized associated
// type and unsizing the box - instead of the trait object being read back into `Box::new`'s
// `T` from a coercion of the whole `Option` ("Unsized type not valid here - dyn Any").
// (combine `parser/combinator.rs:774`)
use std::any::Any;

pub trait Parser {
    type PartialState: Default + Send + 'static;
    fn parse(&self, input: &str, state: &mut Self::PartialState) -> bool;
}

pub struct AnyPartialState(Option<Box<dyn Any + Send>>);

pub struct AnyPartialStateParser<P>(P);

impl<P: Parser> AnyPartialStateParser<P> {
    fn parse_with(&self, input: &str, state: &mut AnyPartialState) -> bool {
        let mut new_child_state;
        let result = {
            let child_state = if state.0.is_none() {
                new_child_state = Some(Default::default());
                new_child_state.as_mut().unwrap()
            } else {
                new_child_state = None;
                state.0.as_mut().unwrap().downcast_mut().unwrap()
            };
            self.0.parse(input, child_state)
        };
        if result && state.0.is_none() {
            state.0 = Some(Box::new(new_child_state.unwrap()));
        }
        result
    }
}

struct Count;

impl Parser for Count {
    type PartialState = usize;
    fn parse(&self, input: &str, state: &mut usize) -> bool {
        *state += input.len();
        true
    }
}

fn main() {
    let parser = AnyPartialStateParser(Count);
    let mut state = AnyPartialState(None);
    assert!(parser.parse_with("abc", &mut state));
    assert!(parser.parse_with("de", &mut state));
    let boxed = state.0.as_ref().unwrap();
    assert_eq!(boxed.downcast_ref::<usize>(), Some(&5));
}
