//
// The beng-proxy JavaScript library.
//
// Author: Max Kellermann <mk@cm4all.com>
//

/**
 * Internal function.  Do not use.
 */
function _beng_proxy_escape(x)
{
    return encodeURIComponent(x).replace('%', '$');
}

function beng_widget_uri(base_uri, session_id, frame, focus, mode,
                         path, translate, view) {
    if (base_uri == null ||
        (mode != null && mode != "focus" && mode != "frame" &&
         mode != "partial" && mode != "proxy" && mode != "save"))
        return null;

    var uri = base_uri + ";session=" + _beng_proxy_escape(session_id);
    if (focus != null) {
        if (mode == "frame")
            mode = "partial";

        uri += "&focus=" + _beng_proxy_escape(focus);
        if (mode == "partial" || mode == "proxy" || mode == "save")
            frame = focus;

        if (frame != null) {
            uri += "&frame=" + _beng_proxy_escape(frame);

            if (view != null)
                uri += "&view=" + _beng_proxy_escape(view);
        }

        if (mode == "proxy")
            uri += "&raw=1";
        if (mode == "save")
            uri += "&save=1";
        if (path != null) {
            var query_string = null;
            var qmark = path.indexOf("?");
            if (qmark >= 0) {
                query_string = path.substring(qmark);
                path = path.substring(0, qmark);
            }
            uri += "&path=" + _beng_proxy_escape(path);
            if (query_string != null)
                uri += query_string;
        }
    }

    if (translate != null)
        uri += "&translate=" + _beng_proxy_escape(translate);

    return uri;
}
