#include "widget_resolver.hxx"
#include "widget_registry.hxx"
#include "async.hxx"
#include "widget.hxx"
#include "widget_class.hxx"
#include "pool.hxx"
#include "RootPool.hxx"
#include "event/Loop.hxx"
#include "util/Cast.hxx"

#include <assert.h>

static struct Context *global;

struct Context {
    struct {
        struct async_operation_ref async_ref;

        bool finished = false;

        /** abort in the callback? */
        bool abort = false;
    } first, second;

    struct Registry final : Cancellable {
        bool requested = false, finished = false, aborted = false;
        WidgetRegistryCallback callback = nullptr;

        /* virtual methods from class Cancellable */
        void Cancel() override {
            aborted = true;
        }
    } registry;

    Context() {
        global = this;
    }

    void ResolverCallback1();
    void ResolverCallback2();
};

const WidgetView *
widget_view_lookup(const WidgetView *view,
                   gcc_unused const char *name)
{
    return view;
}

void
Context::ResolverCallback1()
{
    assert(!first.finished);
    assert(!second.finished);

    first.finished = true;

    if (first.abort)
        second.async_ref.Abort();
}

void
Context::ResolverCallback2()
{
    assert(first.finished);
    assert(!second.finished);
    assert(!second.abort);

    second.finished = true;
}

/*
 * widget-registry.c emulation
 *
 */

void
widget_class_lookup(gcc_unused struct pool &pool,
                    gcc_unused struct pool &widget_pool,
                    gcc_unused struct tcache &translate_cache,
                    gcc_unused const char *widget_type,
                    WidgetRegistryCallback callback,
                    struct async_operation_ref &async_ref)
{
    Context *data = global;
    assert(!data->registry.requested);
    assert(!data->registry.finished);
    assert(!data->registry.aborted);
    assert(!data->registry.callback);

    data->registry.requested = true;
    data->registry.callback = callback;
    async_ref = data->registry;
}

static void
widget_registry_finish(Context *data)
{
    assert(data->registry.requested);
    assert(!data->registry.finished);
    assert(!data->registry.aborted);
    assert(data->registry.callback);

    data->registry.finished = true;

    static const WidgetClass cls = WidgetClass(WidgetClass::Root());
    data->registry.callback(&cls);
}


/*
 * tests
 *
 */

static void
test_normal(struct pool *pool)
{
    Context data;

    pool = pool_new_linear(pool, "test", 8192);

    auto widget = NewFromPool<Widget>(*pool, *pool, nullptr);
    widget->class_name = "foo";

    ResolveWidget(*pool, *widget,
                  *(struct tcache *)(size_t)0x1,
                  BIND_METHOD(data, &Context::ResolverCallback1),
                  data.first.async_ref);

    assert(!data.first.finished);
    assert(!data.second.finished);
    assert(data.registry.requested);
    assert(!data.registry.finished);
    assert(!data.registry.aborted);

    widget_registry_finish(&data);

    assert(data.first.finished);
    assert(!data.second.finished);
    assert(data.registry.requested);
    assert(data.registry.finished);
    assert(!data.registry.aborted);

    pool_unref(pool);
    pool_commit();
}

static void
test_abort(struct pool *pool)
{
    Context data;

    pool = pool_new_linear(pool, "test", 8192);

    auto widget = NewFromPool<Widget>(*pool, *pool, nullptr);
    widget->class_name = "foo";

    ResolveWidget(*pool, *widget,
                  *(struct tcache *)(size_t)0x1,
                  BIND_METHOD(data, &Context::ResolverCallback1),
                  data.first.async_ref);

    assert(!data.first.finished);
    assert(!data.second.finished);
    assert(data.registry.requested);
    assert(!data.registry.finished);
    assert(!data.registry.aborted);

    data.first.async_ref.Abort();

    assert(!data.first.finished);
    assert(!data.second.finished);
    assert(data.registry.requested);
    assert(!data.registry.finished);
    assert(data.registry.aborted);

    pool_unref(pool);
    pool_commit();
}

static void
test_two_clients(struct pool *pool)
{
    Context data;

    pool = pool_new_linear(pool, "test", 8192);

    auto widget = NewFromPool<Widget>(*pool, *pool, nullptr);
    widget->class_name = "foo";

    ResolveWidget(*pool, *widget,
                  *(struct tcache *)(size_t)0x1,
                  BIND_METHOD(data, &Context::ResolverCallback1),
                  data.first.async_ref);

    ResolveWidget(*pool, *widget,
                  *(struct tcache *)(size_t)0x1,
                  BIND_METHOD(data, &Context::ResolverCallback2),
                  data.second.async_ref);

    assert(!data.first.finished);
    assert(!data.second.finished);
    assert(data.registry.requested);
    assert(!data.registry.finished);
    assert(!data.registry.aborted);

    widget_registry_finish(&data);

    assert(data.first.finished);
    assert(data.second.finished);
    assert(data.registry.requested);
    assert(data.registry.finished);
    assert(!data.registry.aborted);

    pool_unref(pool);
    pool_commit();
}

static void
test_two_abort(struct pool *pool)
{
    Context data;
    data.first.abort = true;

    pool = pool_new_linear(pool, "test", 8192);

    auto widget = NewFromPool<Widget>(*pool, *pool, nullptr);
    widget->class_name = "foo";

    ResolveWidget(*pool, *widget,
                  *(struct tcache *)(size_t)0x1,
                  BIND_METHOD(data, &Context::ResolverCallback1),
                  data.first.async_ref);

    ResolveWidget(*pool, *widget,
                  *(struct tcache *)(size_t)0x1,
                  BIND_METHOD(data, &Context::ResolverCallback2),
                  data.second.async_ref);

    assert(!data.first.finished);
    assert(!data.second.finished);
    assert(data.registry.requested);
    assert(!data.registry.finished);
    assert(!data.registry.aborted);

    widget_registry_finish(&data);

    assert(data.first.finished);
    assert(!data.second.finished);
    assert(data.registry.requested);
    assert(data.registry.finished);
    assert(!data.registry.aborted);

    pool_unref(pool);
    pool_commit();
}


/*
 * main
 *
 */

int main(int argc __attr_unused, char **argv __attr_unused) {
    EventLoop event_loop;

    /* run test suite */

    test_normal(RootPool());
    test_abort(RootPool());
    test_two_clients(RootPool());
    test_two_abort(RootPool());
}
