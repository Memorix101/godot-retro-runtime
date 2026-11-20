extends RefCounted
## Writes the placeholder scripts the project is missing.
##
## Every C component needs a small script in the Godot project: it gives the component a
## class name to attach to a node, its exported fields to the inspector and its handlers
## to the signal dock. The runtime knows all of that from the registrations in C and
## prints the scripts (`retro --placeholders`); this puts the ones the project has no
## class for into res://components. A script that exists is never touched: default
## values, doc comments and narrower types are added there by hand.

const Packager := preload("packager.gd")
const OUTPUT_DIR := "res://components"
const MARKER := "#--- "


## Returns {"errors": PackedStringArray, "written": PackedStringArray of script paths,
## "known": int}. With `check_only` nothing is written, but every script the runtime
## prints is checked to be valid GDScript.
static func generate(check_only := false) -> Dictionary:
	var errors := PackedStringArray()
	var result := {"errors": errors, "written": PackedStringArray(), "known": 0}
	var runtime_path := "res://build/pc".path_join(Packager.runtime_exe())
	var runtime := ProjectSettings.globalize_path(runtime_path)
	if not FileAccess.file_exists(runtime_path):
		errors.append("the PC runtime is not built: %s is missing. Run `make` in the project folder first." % runtime_path)
		return result
	var output := []
	var code := OS.execute(runtime, ["--placeholders"], output, true)
	if code != 0:
		errors.append("'%s --placeholders' failed (exit code %d): %s" % [runtime, code, "\n".join(output)])
		return result

	# class name -> script text
	var scripts := {}
	var current := ""
	for line: String in "\n".join(output).replace("\r", "").split("\n"):
		if line.begins_with(MARKER):
			current = line.trim_prefix(MARKER).strip_edges()
			scripts[current] = ""
		elif current != "":
			scripts[current] += line + "\n"

	var known := {}
	for entry: Dictionary in ProjectSettings.get_global_class_list():
		known[String(entry["class"])] = true
	for name_of_class: String in scripts:
		var text: String = scripts[name_of_class].strip_edges() + "\n"
		# The class name is left out for the check: the class may exist already.
		var probe := GDScript.new()
		probe.source_code = text.replace("class_name %s\n" % name_of_class, "")
		if probe.reload() != OK:
			errors.append("the placeholder the runtime prints for '%s' is not valid GDScript" % name_of_class)
			continue
		if known.has(name_of_class):
			result.known += 1
			continue
		var path := OUTPUT_DIR.path_join(name_of_class.to_snake_case() + ".gd")
		if FileAccess.file_exists(path):
			errors.append("'%s' exists but declares no class '%s'; the placeholder was not written"
					% [path, name_of_class])
			continue
		if check_only:
			result.written.append(path)
			continue
		DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(OUTPUT_DIR))
		var file := FileAccess.open(path, FileAccess.WRITE)
		if file == null:
			errors.append("cannot write '%s' (%s)" % [path, error_string(FileAccess.get_open_error())])
			continue
		file.store_string(text)
		file.close()
		result.written.append(path)
	return result
