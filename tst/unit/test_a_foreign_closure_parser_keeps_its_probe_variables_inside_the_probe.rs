//@ aux-build: cut_parser_aux.rs
// winnow's ndjson example: `separated_pair(string, cut_err((ws, ':', ws)),
// json_value)` with `cut_err` and `separated_pair` returning closures of the
// winnow crate. Selecting `FnOnce` for the foreign `cut_err` closure related
// the candidate's parameters inside a probe, and a probe variable that joined a
// variable born in the same probe came out of the probe in the candidate's
// parameters; after the rollback the checker read it from a table that no
// longer had it ("type ivar 46 is not in a table of 39").
use cut_parser_aux::*;

type Input<'i> = Partial<&'i str>;

fn ws<'i, E: ParserError<Input<'i>>>(input: &mut Input<'i>) -> ModalResult<&'i str, E> {
    let _ = input;
    Ok("")
}

fn key_value<'i, E: ParserError<Input<'i>>>(input: &mut Input<'i>) -> ModalResult<(&'i str, &'i str), E> {
    separated_pair(ws, cut_err((ws, ':', ws)), ws).parse_next(input)
}

fn main() {
    let mut input = Partial { input: ":" };
    assert_eq!(key_value::<ContextError>(&mut input).unwrap(), ("", ""));
}
