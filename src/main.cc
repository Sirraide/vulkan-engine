#include "../lib/context.hh"
#include "../lib/window.hh"

int main() {
    vk::context ctx;
    vk::window win{&ctx, 800, 600};

    ctx.run_forever([&] {
        if (ctx.should_terminate = win.should_close(); ctx.should_terminate) return;

        ctx.poll();
    });
}