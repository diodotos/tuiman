export const palette = {
  bg: "#101215",
  panel: "#161b22",
  panelSoft: "#1f2731",
  border: "#405266",
  divider: "#7c6ea8",
  section: "#7db2d3",
  hint: "#9db3c2",
  ok: "#76d6a8",
  warn: "#f0c56d",
  error: "#ef8d8d",
  methodGet: "#88d3a8",
  methodPost: "#f1c76e",
  methodPut: "#88c6f0",
  methodPatch: "#cf9bf2",
  methodDelete: "#f19393",
};

export function methodColor(method: string): string {
  switch (method.toUpperCase()) {
    case "GET":
      return palette.methodGet;
    case "POST":
      return palette.methodPost;
    case "PUT":
      return palette.methodPut;
    case "PATCH":
      return palette.methodPatch;
    case "DELETE":
      return palette.methodDelete;
    default:
      return palette.hint;
  }
}
