"""Allowlist shared by change detection and production deployment."""

REGISTRY = "dev-leo4-ru.cr.cloud.ru/etran"
COMPONENTS = {
    "processingbackend": {
        "directory": "ProcessingBackend/backend",
        "context": ".",
        "project": "user1",
        "service": "processing-backend",
        "compose": "/home/user1/compose.yaml",
    },
    "menubuilder-backend": {
        "directory": "MenuBuilder/backend",
        "context": ".",
        "project": "user1",
        "service": "menubuilder-backend",
        "compose": "/home/user1/compose.yaml",
    },
    "menubuilder-frontend": {
        "directory": "MenuBuilder/frontend",
        "context": "MenuBuilder/frontend",
        "project": "user1",
        "service": "nginx",
        "compose": "/home/user1/compose.yaml",
    },
    "l4mcp": {
        "directory": "MenuBuilder/l4mcp",
        "context": "MenuBuilder/l4mcp",
        "project": "user1",
        "service": "l4mcp",
        "compose": "/home/user1/compose.yaml",
    },
    "l4media-ingress": {
        "directory": "l4media/ingress",
        "context": "l4media/ingress",
        "project": "l4media",
        "service": "ingress",
        "compose": "/home/user1/l4media/compose.yaml",
    },
    "l4media-nginx": {
        "directory": "l4media/nginx",
        "context": "l4media/nginx",
        "project": "l4media",
        "service": "nginx",
        "compose": "/home/user1/l4media/compose.yaml",
    },
}

# app1 is built from the separate iot-rpc-rest-app repository. It uses the
# same digest-only production deployer, but never the etranprocessing builder.
EXTERNAL_COMPONENTS = {
    "app1": {
        "project": "user1",
        "service": "app1",
        "compose": "/home/user1/compose.yaml",
    }
}
DEPLOY_COMPONENTS = {**COMPONENTS, **EXTERNAL_COMPONENTS}


def select_components(paths, event, component="all", bootstrap=""):
    if event not in {"push", "workflow_dispatch"}:
        raise ValueError("Unsupported event")
    if component not in {*COMPONENTS, "all"}:
        raise ValueError("Unknown component")
    if bootstrap not in {"", "menubuilder-backend"}:
        raise ValueError("Bootstrap may only select menubuilder-backend")
    if bootstrap and event == "push":
        selected = {bootstrap}
    elif event == "workflow_dispatch":
        selected = set(COMPONENTS) if component == "all" else {component}
    else:
        selected = set()
        for path in paths:
            if path.endswith(".md"):
                continue
            if path.startswith(".github/") or path in {
                ".dockerignore",
                ".gitattributes",
            }:
                selected.update(COMPONENTS)
            for name, spec in COMPONENTS.items():
                if path.startswith(spec["directory"] + "/"):
                    selected.add(name)
            if path.startswith("shared/"):
                selected.update({"processingbackend", "menubuilder-backend"})
    return {
        "include": [
            {
                "name": name,
                "trigger": name in selected,
                "directory": spec["directory"],
                "context": spec["context"],
                "dockerfile": spec["directory"] + "/Dockerfile",
                "python": name in {"processingbackend", "menubuilder-backend"},
            }
            for name, spec in COMPONENTS.items()
        ]
    }
