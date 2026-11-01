//@ reject-flags: --cfg function_pattern => Attempting to match over a functon pointer || opaque type
//@ reject-flags: --cfg struct_pattern => Attempting to match over a functon pointer || opaque type
//@ reject-flags: --cfg enum_pattern => Attempting to match over a functon pointer || opaque type
//@ compile-variant: -O
#![feature(type_alias_impl_trait)]

type Operation = impl Fn(u16) -> u16;
fn increment(value: u16) -> u16 { value + 3 }
#[define_opaque(Operation)]
const OPERATION: Operation = increment;

struct Record(u16);
type RecordConstructor = impl Fn(u16) -> Record;
#[define_opaque(RecordConstructor)]
const RECORD: RecordConstructor = Record;

enum Choice { Value(u16) }
type ChoiceConstructor = impl Fn(u16) -> Choice;
#[define_opaque(ChoiceConstructor)]
const CHOICE: ChoiceConstructor = Choice::Value;

#[cfg(function_pattern)]
fn reject_function() { match OPERATION { OPERATION => (), _ => () } }
#[cfg(struct_pattern)]
fn reject_struct() { match RECORD { RECORD => (), _ => () } }
#[cfg(enum_pattern)]
fn reject_enum() { match CHOICE { CHOICE => (), _ => () } }

fn main() {
    assert_eq!(OPERATION(11), 14);
    assert_eq!(RECORD(17).0, 17);
    let Choice::Value(value) = CHOICE(23);
    assert_eq!(value, 23);
}
