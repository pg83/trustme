/* combine's `parser!` chain: a `$parser:block` fragment passed through two more macro
   levels ends as `$parser.parse_mode(mode, input, state)` - the tail of a block after
   `let` statements.  A block in statement position may be followed by a method call
   (upstream eats `.` before the statement-expression completeness check). */
macro_rules! parse_partial {
    ((()) $mode:ident $input:ident $state:ident $parser:block) => {{
        let _ = $state;
        let mut state = Default::default();
        let state = &mut state;
        $parser.parse_mode($mode, $input, state)
    }};
    (($ignored:ty) $mode:ident $input:ident $state:ident $parser:block) => {
        $parser.parse_mode($mode, $input, $state)
    };
}
macro_rules! parser_impl {
    ((type PartialState = ($($partial_state: tt)*);) fn $name:ident $parser:block) => {
        fn $name(mode: u32, input: u32, state: &mut u32) -> u32 {
            parse_partial!(($($partial_state)*) mode input state $parser)
        }
    };
}
macro_rules! parser {
    (type PartialState = $partial_state:ty; fn $name:ident $parser:block) => {
        parser_impl! { (type PartialState = ($partial_state);) fn $name $parser }
    };
    (fn $name:ident $parser:block) => {
        parser_impl! { (type PartialState = (());) fn $name $parser }
    };
}
struct P(u32);
impl P {
    fn parse_mode(&self, mode: u32, input: u32, state: &mut u32) -> u32 {
        *state += 1;
        self.0 + mode + input + *state
    }
}
parser! { fn unit { P(1) } }
parser! { type PartialState = u32; fn stateful { P(2) } }
fn main() {
    let mut s = 10;
    assert_eq!(unit(1, 1, &mut s), 4);
    assert_eq!(stateful(1, 1, &mut s), 15);
    assert_eq!(s, 11);
}
