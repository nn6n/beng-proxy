#include "tstock.hxx"
#include "TranslateHandler.hxx"
#include "translate_request.hxx"
#include "translate_response.hxx"
#include "widget_view.hxx"
#include "transformation.hxx"
#include "async.hxx"
#include "fb_pool.hxx"
#include "pool.hxx"
#include "RootPool.hxx"
#include "lhttp_address.hxx"
#include "http_address.hxx"
#include "file_address.hxx"
#include "cgi_address.hxx"
#include "nfs_address.hxx"
#include "event/Base.hxx"

#include <stdio.h>

static void
print_resource_address(const ResourceAddress *address)
{
    switch (address->type) {
    case ResourceAddress::Type::NONE:
        break;

    case ResourceAddress::Type::LOCAL:
        printf("path=%s\n", address->u.file->path);
        if (address->u.file->content_type != nullptr)
            printf("content_type=%s\n",
                   address->u.file->content_type);
        break;

    case ResourceAddress::Type::HTTP:
        printf("proxy=%s\n", address->u.http->path);
        break;

    case ResourceAddress::Type::LHTTP:
        printf("lhttp=%s|%s\n", address->u.lhttp->path, address->u.lhttp->uri);
        break;

    case ResourceAddress::Type::PIPE:
        printf("pipe=%s\n", address->u.cgi->path);
        break;

    case ResourceAddress::Type::CGI:
        printf("cgi=%s\n", address->u.cgi->path);
        break;

    case ResourceAddress::Type::FASTCGI:
        printf("fastcgi=%s\n", address->u.cgi->path);
        break;

    case ResourceAddress::Type::WAS:
        printf("was=%s\n", address->u.cgi->path);
        break;

    case ResourceAddress::Type::AJP:
        printf("ajp=%s\n", address->u.http->path);
        break;

    case ResourceAddress::Type::NFS:
        printf("nfs=%s:%s\n", address->u.nfs->server, address->u.nfs->path);
        break;
    }
}

static void
my_translate_response(TranslateResponse &response, void *ctx)
{
    const WidgetView *view;

    (void)ctx;

    if (response.status != 0)
        printf("status=%d\n", response.status);

    print_resource_address(&response.address);

    for (view = response.views; view != nullptr; view = view->next) {
        if (view->name != nullptr)
            printf("view=%s\n", view->name);

        for (const Transformation *transformation = view->transformation;
             transformation != nullptr;
             transformation = transformation->next) {
            switch (transformation->type) {
            case Transformation::Type::PROCESS:
                printf("process\n");
                break;

            case Transformation::Type::PROCESS_CSS:
                printf("process_css\n");
                break;

            case Transformation::Type::PROCESS_TEXT:
                printf("process_text\n");
                break;

            case Transformation::Type::FILTER:
                printf("filter\n");
                print_resource_address(&transformation->u.filter.address);
                break;
            }
        }
    }

    if (response.redirect != nullptr)
        printf("redirect=%s\n", response.redirect);
    if (response.session.IsNull())
        printf("session=%.*s\n", (int)response.session.size,
               (const char *)response.session.data);
    if (response.user != nullptr)
        printf("user=%s\n", response.user);
}

static void
my_translate_error(GError *error, gcc_unused void *ctx)
{
    fprintf(stderr, "%s\n", error->message);
    g_error_free(error);
}

static const TranslateHandler my_translate_handler = {
    .response = my_translate_response,
    .error = my_translate_error,
};

int main(int argc, char **argv) {
    TranslateRequest request;
    request.Clear();
    request.host = "example.com";
    request.uri = "/foo/index.html";

    struct async_operation_ref async_ref;

    (void)argc;
    (void)argv;

    EventBase event_base;
    fb_pool_init(false);

    RootPool pool;

    auto *translate_stock = tstock_new(*pool, "@translation", 0);

    tstock_translate(*translate_stock, *pool,
                     request, my_translate_handler, nullptr, async_ref);

    event_base.Dispatch();
    fb_pool_deinit();
}
