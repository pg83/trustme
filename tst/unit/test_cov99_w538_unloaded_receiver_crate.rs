//@ aux-build: cov99_w538_unloaded_receiver.rs
//@ compile-fail: Unknown receiver type
//@ compile-flags: --emit=metadata -Cpanic=abort

macro_rules! receiver_probe {
    () => {
        trait Probe {
            fn probe(self: ::"cov99_w538_unloaded_receiver"::Receiver<()>);
        }
    };
}

receiver_probe!();

fn main() {}
