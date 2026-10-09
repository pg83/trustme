// `&[version]` passed for a `&[&'static Version]` parameter: rustc checks the
// borrow's operand under `ExpectRvalueLikeUnsized([&Version])`
// (`check_expr_addr_of` through `rvalue_hint`, since the pointee is unsized),
// and `check_expr_array` takes the element type it coerces every element to
// from that expectation (`to_option` sees it, `only_has_type` does not).
// `version` is a `&&Version`, so the element coerces by deref into
// `&Version`. Without the expectation the element type is a fresh variable,
// the element binds it to `&&Version`, and the array no longer unsizes into
// the slice. rustls' `close_notify_server_to_client` test is this shape.
use std::sync::Arc;

#[derive(Debug)]
pub struct Version(u8);

pub static ALL_VERSIONS: &[&Version] = &[&Version(1), &Version(2)];

#[derive(Clone, Copy)]
enum KeyType {
    Rsa,
}

struct Provider;

fn default_provider() -> Provider {
    Provider
}

struct ServerConfig(u8);

struct ClientConfig;

fn make_server_config(_: KeyType, versions: &[&'static Version], _: &Provider) -> ServerConfig {
    ServerConfig(versions[0].0)
}

fn make_client_config(_: KeyType, _: &Provider) -> ClientConfig {
    ClientConfig
}

#[derive(Clone)]
struct Actions {
    send_close_notify: bool,
}

const NO_ACTIONS: Actions = Actions { send_close_notify: false };

struct Outcome {
    client_saw_peer_closed_state: bool,
}

fn run(_: Arc<ClientConfig>, _: &mut Actions, server: Arc<ServerConfig>, server_actions: &mut Actions) -> Outcome {
    server_actions.send_close_notify = false;
    Outcome { client_saw_peer_closed_state: server.0 > 0 }
}

fn main() {
    let provider = default_provider();
    for version in ALL_VERSIONS {
        eprintln!("{version:?}");
        let server_config = make_server_config(KeyType::Rsa, &[version], &provider);
        let client_config = make_client_config(KeyType::Rsa, &provider);

        let mut server_actions = Actions {
            send_close_notify: true,
            ..NO_ACTIONS
        };

        let outcome = run(
            Arc::new(client_config),
            &mut NO_ACTIONS.clone(),
            Arc::new(server_config),
            &mut server_actions,
        );

        assert!(!server_actions.send_close_notify);
        assert!(outcome.client_saw_peer_closed_state);
    }
}
