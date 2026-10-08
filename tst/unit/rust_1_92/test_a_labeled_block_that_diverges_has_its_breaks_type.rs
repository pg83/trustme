// A labeled block takes its value from its `break`s and from its own end.
// A block with no tail expression supplies `()` only when its end is
// reachable (`check_block_with_expected` in rustc_hir_typeck): a last
// statement `match .. { .. => break 'blk .., .. => break 'label .. };` that
// diverges in every arm supplies nothing. zlib-rs's inflate state machine is
// `mode = 'blk: { match mode { .. } };` inside a labeled loop.
#[derive(Clone, Copy, PartialEq, Debug)]
enum Mode {
    Head,
    Body,
    Done,
}

fn run(mut mode: Mode, steps: u32) -> (Mode, u32) {
    let mut taken = 0;
    let code = 'label: loop {
        mode = 'blk: {
            match mode {
                Mode::Head => {
                    taken += 1;
                    break 'blk Mode::Body;
                }
                Mode::Body => {
                    if taken >= steps {
                        break 'label 7;
                    }
                    taken += 1;
                    break 'blk Mode::Done;
                }
                Mode::Done => {
                    break 'label 1;
                }
            };
        };
    };
    (mode, code + taken)
}

fn block_of_statements(x: u8) -> u8 {
    let doubled: u8 = 'blk: {
        {
            break 'blk x * 2;
        };
    };
    doubled
}

fn main() {
    assert_eq!(run(Mode::Head, 5), (Mode::Done, 3));
    assert_eq!(run(Mode::Head, 1), (Mode::Body, 8));
    assert_eq!(block_of_statements(4), 8);
}
