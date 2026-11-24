// Unity editor script: writes a Unity scene out in a form a Godot tool script can read.
//
// Copy this file and RetroSceneExport.asmdef into Assets/Editor/RetroSceneExport/ of a
// Unity project and run, from the command line:
//
//   Unity.exe -batchmode -projectPath <project> -executeMethod RetroSceneExport.Run
//       -exportScene Assets/Scenes/Game1.unity -exportOut <folder> -quit
//
// The script has its own assembly and references no package and no game script, so it
// compiles even when the project's own scripts do not. Everything is read through
// SerializedObject, which also gives the fields of game scripts and of packages such as
// TextMeshPro.
//
// What is written to <folder>:
//
//   scene.json        node trees of the scene and of the prefabs its scripts refer to,
//                     materials, textures, sounds, fonts, terrains (format below)
//   meshes/*.mesh     one mesh each (binary, little endian, see WriteMesh)
//   textures/*        copies of the source images, or PNGs where the source is not an
//                     image Godot reads
//   audio/*, fonts/*  copies of the source files
//   terrain/*.r32     heights of a terrain: float32, 0..1 of the terrain's height,
//                     row 0 is the far side (Godot -Z), column 0 is -X
//   terrain/*_color.png  the terrain's look: the average colour of every terrain layer,
//                     mixed by the splat map, same orientation as the heights
//
// All positions, rotations and meshes are converted to Godot's axes: Unity's Z is
// negated (left-handed to right-handed), so Unity's forward (+Z) becomes Godot's
// forward (-Z). Texture coordinates get v = 1 - v. Triangles are turned so the fronts
// stay the fronts.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

public static class RetroSceneExport
{
    static string outDir;
    static readonly StringBuilder log = new StringBuilder();

    // Assets written so far, by instance id, and the key they are listed under.
    static readonly Dictionary<UnityEngine.Object, string> meshKeys = new Dictionary<UnityEngine.Object, string>();
    static readonly Dictionary<UnityEngine.Object, string> materialKeys = new Dictionary<UnityEngine.Object, string>();
    static readonly Dictionary<UnityEngine.Object, string> textureKeys = new Dictionary<UnityEngine.Object, string>();
    static readonly Dictionary<UnityEngine.Object, string> audioKeys = new Dictionary<UnityEngine.Object, string>();
    static readonly Dictionary<UnityEngine.Object, string> fontKeys = new Dictionary<UnityEngine.Object, string>();
    static readonly Dictionary<UnityEngine.Object, string> terrainKeys = new Dictionary<UnityEngine.Object, string>();
    static readonly Dictionary<UnityEngine.Object, string> prefabKeys = new Dictionary<UnityEngine.Object, string>();
    static readonly HashSet<string> usedNames = new HashSet<string>();

    static readonly Json materials = Json.Obj();
    static readonly Json textures = Json.Obj();
    static readonly Json audio = Json.Obj();
    static readonly Json fonts = Json.Obj();
    static readonly Json meshes = Json.Obj();
    static readonly Json terrains = Json.Obj();
    static readonly Json prefabs = Json.Obj();
    static readonly List<GameObject> prefabQueue = new List<GameObject>();

    // Node index of every GameObject of the tree being written.
    static Dictionary<GameObject, int> nodeIndex;

    public static void Run()
    {
        string scenePath = Arg("-exportScene");
        outDir = Arg("-exportOut");
        if (scenePath == null || outDir == null)
        {
            Debug.LogError("RetroSceneExport: needs -exportScene and -exportOut");
            EditorApplication.Exit(1);
            return;
        }
        try
        {
            Export(scenePath);
            EditorApplication.Exit(0);
        }
        catch (Exception e)
        {
            Debug.LogException(e);
            EditorApplication.Exit(1);
        }
    }

    static string Arg(string name)
    {
        string[] args = Environment.GetCommandLineArgs();
        for (int i = 0; i < args.Length - 1; i++)
            if (args[i] == name)
                return args[i + 1];
        return null;
    }

    static void Export(string scenePath)
    {
        foreach (string sub in new[] { "", "meshes", "textures", "audio", "fonts", "terrain" })
            Directory.CreateDirectory(Path.Combine(outDir, sub));

        var scene = EditorSceneManager.OpenScene(scenePath, OpenSceneMode.Single);
        var root = Json.Obj();
        root["unity_version"] = Application.unityVersion;
        root["scene"] = scenePath;

        var sceneNodes = WriteTree(new List<GameObject>(scene.GetRootGameObjects()));
        root["nodes"] = sceneNodes;

        // Prefabs that scripts refer to (bullets, effects, trees). New ones can turn up
        // while one is written.
        for (int i = 0; i < prefabQueue.Count; i++)
        {
            GameObject prefab = prefabQueue[i];
            var entry = Json.Obj();
            entry["path"] = AssetDatabase.GetAssetPath(prefab);
            entry["nodes"] = WriteTree(new List<GameObject> { prefab });
            prefabs[prefabKeys[prefab]] = entry;
        }

        root["prefabs"] = prefabs;
        root["meshes"] = meshes;
        root["materials"] = materials;
        root["textures"] = textures;
        root["audio"] = audio;
        root["fonts"] = fonts;
        root["terrains"] = terrains;
        root["render"] = RenderSettingsJson();
        root["tags"] = Json.Arr(UnityEditorInternal.InternalEditorUtility.tags);
        var layers = Json.Arr();
        for (int i = 0; i < 32; i++)
            layers.Add(LayerMask.LayerToName(i));
        root["layers"] = layers;

        File.WriteAllText(Path.Combine(outDir, "scene.json"), root.ToString(), new UTF8Encoding(false));
        File.WriteAllText(Path.Combine(outDir, "export.log"), log.ToString());
        Debug.Log("RetroSceneExport: wrote " + outDir);
    }

    // ------------------------------------------------------------------------------------
    // Node trees

    static Json WriteTree(List<GameObject> roots)
    {
        var saved = nodeIndex;
        nodeIndex = new Dictionary<GameObject, int>();
        var order = new List<GameObject>();
        foreach (var r in roots)
            Collect(r, order);
        for (int i = 0; i < order.Count; i++)
            nodeIndex[order[i]] = i;
        var nodes = Json.Arr();
        foreach (var go in order)
            nodes.Add(WriteNode(go));
        nodeIndex = saved;
        return nodes;
    }

    static void Collect(GameObject go, List<GameObject> order)
    {
        order.Add(go);
        foreach (Transform child in go.transform)
            Collect(child.gameObject, order);
    }

    static Json WriteNode(GameObject go)
    {
        var n = Json.Obj();
        var t = go.transform;
        n["name"] = go.name;
        n["parent"] = t.parent != null && nodeIndex.ContainsKey(t.parent.gameObject)
            ? nodeIndex[t.parent.gameObject] : -1;
        n["active"] = go.activeSelf;
        n["tag"] = go.tag;
        n["layer"] = go.layer;
        n["static"] = go.isStatic;
        n["position"] = Pos(t.localPosition);
        n["rotation"] = Rot(t.localRotation);
        n["scale"] = Vec(t.localScale);
        n["global_position"] = Pos(t.position);
        n["global_rotation"] = Rot(t.rotation);
        n["global_scale"] = Vec(t.lossyScale);
        if (PrefabUtility.IsAnyPrefabInstanceRoot(go))
        {
            var src = PrefabUtility.GetCorrespondingObjectFromSource(go);
            if (src != null)
                n["prefab_source"] = AssetDatabase.GetAssetPath(src);
        }

        var comps = Json.Arr();
        foreach (var c in go.GetComponents<Component>())
        {
            if (c == null)
            {
                comps.Add(Json.Obj().Set("type", "MissingScript"));
                continue;
            }
            try
            {
                comps.Add(WriteComponent(c));
            }
            catch (Exception e)
            {
                Warn(go.name + ": " + c.GetType().Name + ": " + e.Message);
                comps.Add(Json.Obj().Set("type", c.GetType().Name).Set("error", e.Message));
            }
        }
        n["components"] = comps;
        return n;
    }

    static Json WriteComponent(Component c)
    {
        var j = Json.Obj();
        j["type"] = c.GetType().Name;
        if (c is Behaviour b)
            j["enabled"] = b.enabled;

        switch (c)
        {
            case RectTransform rt:
                j["anchor_min"] = Vec(rt.anchorMin);
                j["anchor_max"] = Vec(rt.anchorMax);
                j["pivot"] = Vec(rt.pivot);
                j["anchored_position"] = Vec(rt.anchoredPosition);
                j["size_delta"] = Vec(rt.sizeDelta);
                j["offset_min"] = Vec(rt.offsetMin);
                j["offset_max"] = Vec(rt.offsetMax);
                j["rect"] = Json.Arr(rt.rect.x, rt.rect.y, rt.rect.width, rt.rect.height);
                j["unity_euler"] = Vec(rt.localEulerAngles);
                j["unity_scale"] = Vec(rt.localScale);
                break;
            case Transform _:
                break;
            case MeshFilter mf:
                j["mesh"] = MeshKey(mf.sharedMesh);
                break;
            case SkinnedMeshRenderer smr:
                // Taken as a rigid mesh in its bind pose.
                j["mesh"] = MeshKey(smr.sharedMesh);
                j["materials"] = MaterialList(smr.sharedMaterials);
                j["renderer_enabled"] = smr.enabled;
                break;
            case MeshRenderer mr:
                j["materials"] = MaterialList(mr.sharedMaterials);
                j["renderer_enabled"] = mr.enabled;
                j["cast_shadows"] = mr.shadowCastingMode.ToString();
                break;
            case BoxCollider bc:
                j["trigger"] = bc.isTrigger;
                j["collider_enabled"] = bc.enabled;
                j["center"] = Pos(bc.center);
                j["size"] = Vec(bc.size);
                break;
            case SphereCollider sc:
                j["trigger"] = sc.isTrigger;
                j["collider_enabled"] = sc.enabled;
                j["center"] = Pos(sc.center);
                j["radius"] = sc.radius;
                break;
            case CapsuleCollider cc:
                j["trigger"] = cc.isTrigger;
                j["collider_enabled"] = cc.enabled;
                j["center"] = Pos(cc.center);
                j["radius"] = cc.radius;
                j["height"] = cc.height;
                j["direction"] = cc.direction;
                break;
            case MeshCollider mc:
                j["trigger"] = mc.isTrigger;
                j["collider_enabled"] = mc.enabled;
                j["convex"] = mc.convex;
                j["mesh"] = MeshKey(mc.sharedMesh);
                break;
            case TerrainCollider tc:
                j["collider_enabled"] = tc.enabled;
                j["terrain"] = TerrainKey(tc.terrainData);
                break;
            case Rigidbody rb:
                j["mass"] = rb.mass;
                j["kinematic"] = rb.isKinematic;
                j["gravity"] = rb.useGravity;
                j["constraints"] = (int)rb.constraints;
                j["interpolation"] = rb.interpolation.ToString();
                j["collision_detection"] = rb.collisionDetectionMode.ToString();
                break;
            case Camera cam:
                j["fov"] = cam.fieldOfView;
                j["near"] = cam.nearClipPlane;
                j["far"] = cam.farClipPlane;
                j["orthographic"] = cam.orthographic;
                j["ortho_size"] = cam.orthographicSize;
                j["clear_flags"] = cam.clearFlags.ToString();
                j["background"] = Col(cam.backgroundColor);
                j["depth"] = cam.depth;
                j["culling_mask"] = cam.cullingMask;
                j["viewport"] = Json.Arr(cam.rect.x, cam.rect.y, cam.rect.width, cam.rect.height);
                j["target_texture"] = cam.targetTexture != null;
                break;
            case Light l:
                j["light_type"] = l.type.ToString();
                j["color"] = Col(l.color);
                j["intensity"] = l.intensity;
                j["range"] = l.range;
                j["spot_angle"] = l.spotAngle;
                j["shadows"] = l.shadows.ToString();
                break;
            case AudioSource src:
                j["clip"] = AudioKey(src.clip);
                j["volume"] = src.volume;
                j["pitch"] = src.pitch;
                j["loop"] = src.loop;
                j["play_on_awake"] = src.playOnAwake;
                j["spatial_blend"] = src.spatialBlend;
                j["min_distance"] = src.minDistance;
                j["max_distance"] = src.maxDistance;
                j["rolloff"] = src.rolloffMode.ToString();
                break;
            case Terrain ter:
                j["terrain"] = TerrainKey(ter.terrainData);
                j["material"] = MaterialKey(ter.materialTemplate);
                j["draw_trees"] = ter.drawTreesAndFoliage;
                break;
            case Canvas cv:
                j["render_mode"] = cv.renderMode.ToString();
                j["sort_order"] = cv.sortingOrder;
                j["plane_distance"] = cv.planeDistance;
                break;
            case ParticleSystem ps:
                WriteParticles(ps, j);
                break;
            case LineRenderer _:
            case TrailRenderer _:
                break;
            default:
                // Game scripts and everything else: their serialized fields.
                j["fields"] = SerializedFields(c);
                if (c is MonoBehaviour mb)
                {
                    var script = MonoScript.FromMonoBehaviour(mb);
                    if (script != null)
                        j["script"] = AssetDatabase.GetAssetPath(script);
                }
                break;
        }
        return j;
    }

    static void WriteParticles(ParticleSystem ps, Json j)
    {
        var main = ps.main;
        j["duration"] = main.duration;
        j["looping"] = main.loop;
        j["play_on_awake"] = main.playOnAwake;
        j["max_particles"] = main.maxParticles;
        j["start_lifetime"] = Curve(main.startLifetime);
        j["start_speed"] = Curve(main.startSpeed);
        j["start_size"] = Curve(main.startSize);
        j["start_color"] = Col(main.startColor.color);
        j["start_color_max"] = Col(main.startColor.colorMax);
        j["gravity"] = Curve(main.gravityModifier);
        j["simulation_space"] = main.simulationSpace.ToString();
        var em = ps.emission;
        j["emission_enabled"] = em.enabled;
        j["rate_over_time"] = Curve(em.rateOverTime);
        var bursts = Json.Arr();
        for (int i = 0; i < em.burstCount; i++)
        {
            var burst = em.GetBurst(i);
            bursts.Add(Json.Obj().Set("time", burst.time).Set("count", Curve(burst.count)));
        }
        j["bursts"] = bursts;
        var shape = ps.shape;
        j["shape_enabled"] = shape.enabled;
        j["shape"] = shape.shapeType.ToString();
        j["shape_angle"] = shape.angle;
        j["shape_radius"] = shape.radius;
        j["shape_scale"] = Vec(shape.scale);
        j["shape_rotation"] = Vec(shape.rotation);
        var col = ps.colorOverLifetime;
        j["color_over_lifetime"] = col.enabled;
        if (col.enabled)
        {
            var g = col.color.gradient;
            if (g != null)
            {
                var keys = Json.Arr();
                foreach (var k in g.colorKeys)
                    keys.Add(Json.Obj().Set("t", k.time).Set("color", Col(k.color)));
                var akeys = Json.Arr();
                foreach (var k in g.alphaKeys)
                    akeys.Add(Json.Obj().Set("t", k.time).Set("alpha", k.alpha));
                j["gradient_colors"] = keys;
                j["gradient_alphas"] = akeys;
            }
        }
        var sz = ps.sizeOverLifetime;
        j["size_over_lifetime"] = sz.enabled;
        var r = ps.GetComponent<ParticleSystemRenderer>();
        if (r != null)
        {
            j["render_mode"] = r.renderMode.ToString();
            j["material"] = MaterialKey(r.sharedMaterial);
            if (r.renderMode == ParticleSystemRenderMode.Mesh)
                j["mesh"] = MeshKey(r.mesh);
        }
    }

    static Json Curve(ParticleSystem.MinMaxCurve c)
    {
        switch (c.mode)
        {
            case ParticleSystemCurveMode.Constant:
                return Json.Arr(c.constant, c.constant);
            case ParticleSystemCurveMode.TwoConstants:
                return Json.Arr(c.constantMin, c.constantMax);
            default:
                return Json.Arr(c.curveMultiplier * 0.0f, c.curveMultiplier);
        }
    }

    // ------------------------------------------------------------------------------------
    // Serialized fields of scripts

    static Json SerializedFields(Component c)
    {
        var fields = Json.Obj();
        var so = new SerializedObject(c);
        var it = so.GetIterator();
        if (!it.NextVisible(true))
            return fields;
        do
        {
            if (it.name == "m_Script")
                continue;
            fields[it.name] = PropertyValue(it.Copy(), 0);
        } while (it.NextVisible(false));
        return fields;
    }

    static Json PropertyValue(SerializedProperty p, int depth)
    {
        if (depth > 6)
            return Json.Null();
        if (p.isArray && p.propertyType != SerializedPropertyType.String)
        {
            var arr = Json.Arr();
            int n = Math.Min(p.arraySize, 4096);
            for (int i = 0; i < n; i++)
                arr.Add(PropertyValue(p.GetArrayElementAtIndex(i), depth + 1));
            return arr;
        }
        switch (p.propertyType)
        {
            case SerializedPropertyType.Integer: return Json.Num(p.longValue);
            case SerializedPropertyType.Boolean: return Json.Bool(p.boolValue);
            case SerializedPropertyType.Float: return Json.Num(p.doubleValue);
            case SerializedPropertyType.String: return Json.Str(p.stringValue);
            case SerializedPropertyType.Color: return Col(p.colorValue);
            case SerializedPropertyType.Enum: return Json.Num(p.intValue);
            case SerializedPropertyType.LayerMask: return Json.Num(p.intValue);
            case SerializedPropertyType.Vector2: return Vec(p.vector2Value);
            case SerializedPropertyType.Vector3: return Vec(p.vector3Value);
            case SerializedPropertyType.Vector4: return Vec(p.vector4Value);
            case SerializedPropertyType.Quaternion:
                var q = p.quaternionValue;
                return Json.Arr(q.x, q.y, q.z, q.w);
            case SerializedPropertyType.Rect:
                var r = p.rectValue;
                return Json.Arr(r.x, r.y, r.width, r.height);
            case SerializedPropertyType.ObjectReference:
                return Reference(p.objectReferenceValue);
            case SerializedPropertyType.Generic:
                {
                    var obj = Json.Obj();
                    var end = p.GetEndProperty();
                    var child = p.Copy();
                    if (!child.NextVisible(true))
                        return obj;
                    while (!SerializedProperty.EqualContents(child, end))
                    {
                        obj[child.name] = PropertyValue(child.Copy(), depth + 1);
                        if (!child.NextVisible(false))
                            break;
                    }
                    return obj;
                }
            default:
                return Json.Str("<" + p.propertyType + ">");
        }
    }

    // A reference to a node of the tree being written, or to an asset.
    static Json Reference(UnityEngine.Object o)
    {
        if (o == null)
            return Json.Null();
        var j = Json.Obj();
        j["ref_type"] = o.GetType().Name;
        GameObject go = o as GameObject;
        if (go == null && o is Component comp)
            go = comp.gameObject;
        if (go != null && nodeIndex != null && nodeIndex.ContainsKey(go))
        {
            j["node"] = nodeIndex[go];
            return j;
        }
        if (go != null && EditorUtility.IsPersistent(go))
        {
            // A prefab asset (or a part of one): written as a tree of its own.
            GameObject root = go.transform.root.gameObject;
            j["prefab"] = PrefabKey(root);
            if (root != go)
                j["prefab_part"] = go.name;
            return j;
        }
        j["asset"] = AssetDatabase.GetAssetPath(o);
        switch (o)
        {
            case AudioClip clip: j["audio"] = AudioKey(clip); break;
            case Material mat: j["material"] = MaterialKey(mat); break;
            case Sprite sp:
                j["texture"] = TextureKey(sp.texture);
                j["sprite_rect"] = Json.Arr(sp.rect.x, sp.texture.height - sp.rect.yMax, sp.rect.width, sp.rect.height);
                j["sprite_border"] = Vec(sp.border);
                break;
            case Texture tex: j["texture"] = TextureKey(tex); break;
            case Mesh mesh: j["mesh"] = MeshKey(mesh); break;
            case Font f: j["font"] = FontKey(f); break;
            default:
                // A TextMeshPro font asset names the font it was made from: by reference
                // (the field has had several names) or by the GUID in its creation settings.
                if (o.GetType().Name == "TMP_FontAsset")
                {
                    var so = new SerializedObject(o);
                    Font source = null;
                    foreach (string field in new[] { "m_SourceFontFile_EditorRef", "m_SourceFontFile" })
                    {
                        var src = so.FindProperty(field);
                        if (source == null && src != null && src.objectReferenceValue is Font tf)
                            source = tf;
                    }
                    if (source == null)
                    {
                        foreach (string field in new[] { "m_SourceFontFileGUID", "m_CreationSettings.sourceFontFileGUID" })
                        {
                            var guid = so.FindProperty(field);
                            if (source == null && guid != null && !string.IsNullOrEmpty(guid.stringValue))
                                source = AssetDatabase.LoadAssetAtPath<Font>(AssetDatabase.GUIDToAssetPath(guid.stringValue));
                        }
                    }
                    if (source != null)
                        j["font"] = FontKey(source);
                    else
                        Warn("no source font for " + AssetDatabase.GetAssetPath(o));
                    j["name"] = o.name;
                }
                else if (o.GetType().Name == "TMP_ColorGradient")
                {
                    j["fields"] = AssetFields(o);
                }
                else if (o is ScriptableObject)
                {
                    j["fields"] = AssetFields(o);
                }
                break;
        }
        return j;
    }

    static Json AssetFields(UnityEngine.Object o)
    {
        var fields = Json.Obj();
        var so = new SerializedObject(o);
        var it = so.GetIterator();
        if (!it.NextVisible(true))
            return fields;
        do
        {
            if (it.name == "m_Script")
                continue;
            fields[it.name] = PropertyValue(it.Copy(), 3);
        } while (it.NextVisible(false));
        return fields;
    }

    // ------------------------------------------------------------------------------------
    // Assets

    static string UniqueName(string prefix, string name)
    {
        string clean = "";
        foreach (char ch in name)
            clean += char.IsLetterOrDigit(ch) || ch == '_' || ch == '-' ? ch : '_';
        if (clean.Length == 0)
            clean = "unnamed";
        string key = prefix + clean;
        int k = 2;
        while (usedNames.Contains(key))
            key = prefix + clean + "_" + k++;
        usedNames.Add(key);
        return key.Substring(prefix.Length);
    }

    static string PrefabKey(GameObject prefab)
    {
        var id = prefab;
        if (prefabKeys.TryGetValue(id, out var key))
            return key;
        key = UniqueName("prefab:", prefab.name);
        prefabKeys[id] = key;
        prefabQueue.Add(prefab);
        return key;
    }

    static Json MaterialList(Material[] mats)
    {
        var arr = Json.Arr();
        foreach (var m in mats)
            arr.Add(MaterialKey(m));
        return arr;
    }

    static Json MaterialKey(Material m)
    {
        if (m == null)
            return Json.Null();
        var id = m;
        if (materialKeys.TryGetValue(id, out var key))
            return Json.Str(key);
        key = UniqueName("material:", m.name);
        materialKeys[id] = key;

        var j = Json.Obj();
        j["asset"] = AssetDatabase.GetAssetPath(m);
        j["shader"] = m.shader != null ? m.shader.name : "";
        j["render_queue"] = m.renderQueue;
        j["keywords"] = Json.Arr(m.shaderKeywords);
        var colors = Json.Obj();
        var texs = Json.Obj();
        var floats = Json.Obj();
        if (m.shader != null)
        {
            int count = m.shader.GetPropertyCount();
            for (int i = 0; i < count; i++)
            {
                string pname = m.shader.GetPropertyName(i);
                switch (m.shader.GetPropertyType(i))
                {
                    case UnityEngine.Rendering.ShaderPropertyType.Color:
                        colors[pname] = Col(m.GetColor(pname));
                        break;
                    case UnityEngine.Rendering.ShaderPropertyType.Float:
                    case UnityEngine.Rendering.ShaderPropertyType.Range:
                        floats[pname] = m.GetFloat(pname);
                        break;
                    case UnityEngine.Rendering.ShaderPropertyType.Texture:
                        var tex = m.GetTexture(pname);
                        if (tex != null)
                        {
                            var tj = Json.Obj();
                            tj["texture"] = TextureKey(tex);
                            tj["scale"] = Vec(m.GetTextureScale(pname));
                            tj["offset"] = Vec(m.GetTextureOffset(pname));
                            texs[pname] = tj;
                        }
                        break;
                }
            }
        }
        j["colors"] = colors;
        j["textures"] = texs;
        j["floats"] = floats;
        materials[key] = j;
        return Json.Str(key);
    }

    static Json TextureKey(Texture tex)
    {
        if (tex == null)
            return Json.Null();
        var id = tex;
        if (textureKeys.TryGetValue(id, out var key))
            return Json.Str(key);
        key = UniqueName("texture:", tex.name);
        textureKeys[id] = key;

        var j = Json.Obj();
        string src = AssetDatabase.GetAssetPath(tex);
        j["asset"] = src;
        j["width"] = tex.width;
        j["height"] = tex.height;
        j["wrap"] = tex.wrapMode.ToString();
        j["filter"] = tex.filterMode.ToString();
        string ext = Path.GetExtension(src).ToLowerInvariant();
        string file;
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp")
        {
            file = "textures/" + key + ext;
            File.Copy(src, Path.Combine(outDir, file), true);
        }
        else
        {
            file = "textures/" + key + ".png";
            var png = ReadPixels(tex, tex.width, tex.height);
            if (png != null)
                File.WriteAllBytes(Path.Combine(outDir, file), png.EncodeToPNG());
            else
                file = null;
        }
        j["file"] = file;
        var importer = AssetImporter.GetAtPath(src) as TextureImporter;
        if (importer != null)
        {
            j["max_size"] = importer.maxTextureSize;
            j["normal_map"] = importer.textureType == TextureImporterType.NormalMap;
            j["alpha_is_transparency"] = importer.alphaIsTransparency;
        }
        textures[key] = j;
        return Json.Str(key);
    }

    // The pixels of any texture, through the GPU, so the asset need not be readable.
    static Texture2D ReadPixels(Texture tex, int w, int h)
    {
        if (tex == null || w <= 0 || h <= 0)
            return null;
        var rt = RenderTexture.GetTemporary(w, h, 0, RenderTextureFormat.ARGB32, RenderTextureReadWrite.sRGB);
        var prev = RenderTexture.active;
        Graphics.Blit(tex, rt);
        RenderTexture.active = rt;
        var copy = new Texture2D(w, h, TextureFormat.RGBA32, false);
        copy.ReadPixels(new Rect(0, 0, w, h), 0, 0);
        copy.Apply();
        RenderTexture.active = prev;
        RenderTexture.ReleaseTemporary(rt);
        return copy;
    }

    static Json AudioKey(AudioClip clip)
    {
        if (clip == null)
            return Json.Null();
        var id = clip;
        if (audioKeys.TryGetValue(id, out var key))
            return Json.Str(key);
        key = UniqueName("audio:", clip.name);
        audioKeys[id] = key;
        string src = AssetDatabase.GetAssetPath(clip);
        string file = "audio/" + key + Path.GetExtension(src).ToLowerInvariant();
        File.Copy(src, Path.Combine(outDir, file), true);
        var j = Json.Obj();
        j["asset"] = src;
        j["file"] = file;
        j["length"] = clip.length;
        j["channels"] = clip.channels;
        j["frequency"] = clip.frequency;
        audio[key] = j;
        return Json.Str(key);
    }

    static Json FontKey(Font f)
    {
        if (f == null)
            return Json.Null();
        var id = f;
        if (fontKeys.TryGetValue(id, out var key))
            return Json.Str(key);
        key = UniqueName("font:", f.name);
        fontKeys[id] = key;
        string src = AssetDatabase.GetAssetPath(f);
        var j = Json.Obj();
        j["asset"] = src;
        string ext = Path.GetExtension(src).ToLowerInvariant();
        if (ext == ".ttf" || ext == ".otf")
        {
            string file = "fonts/" + key + ext;
            File.Copy(src, Path.Combine(outDir, file), true);
            j["file"] = file;
        }
        fonts[key] = j;
        return Json.Str(key);
    }

    static Json MeshKey(Mesh mesh)
    {
        if (mesh == null)
            return Json.Null();
        var id = mesh;
        if (meshKeys.TryGetValue(id, out var key))
            return Json.Str(key);
        key = UniqueName("mesh:", mesh.name);
        meshKeys[id] = key;
        var j = Json.Obj();
        j["asset"] = AssetDatabase.GetAssetPath(mesh);
        j["file"] = "meshes/" + key + ".mesh";
        j["vertices"] = mesh.vertexCount;
        j["submeshes"] = mesh.subMeshCount;
        var b = mesh.bounds;
        j["bounds_center"] = Pos(b.center);
        j["bounds_size"] = Vec(b.size);
        WriteMesh(mesh, Path.Combine(outDir, "meshes", key + ".mesh"));
        meshes[key] = j;
        return Json.Str(key);
    }

    // Layout of a .mesh file (all little endian):
    //   "RMSH", u32 version = 1, u32 vertex count, u32 flags (1 normals, 2 uv, 4 uv2,
    //   8 colours), u32 submesh count;
    //   positions (3 floats each), normals (3), uvs (2), uv2s (2), colours (4 floats);
    //   per submesh: u32 index count, then the indices (u32), three per triangle.
    static void WriteMesh(Mesh mesh, string path)
    {
        if (!mesh.isReadable)
        {
            // Not readable at runtime, but the editor can still copy it.
            mesh = UnityEngine.Object.Instantiate(mesh);
        }
        var v = mesh.vertices;
        var nrm = mesh.normals;
        var uv = mesh.uv;
        var uv2 = mesh.uv2;
        var col = mesh.colors;
        uint flags = 0;
        if (nrm.Length == v.Length) flags |= 1;
        if (uv.Length == v.Length) flags |= 2;
        if (uv2.Length == v.Length) flags |= 4;
        if (col.Length == v.Length) flags |= 8;
        using (var w = new BinaryWriter(File.Create(path)))
        {
            w.Write(Encoding.ASCII.GetBytes("RMSH"));
            w.Write(1u);
            w.Write((uint)v.Length);
            w.Write(flags);
            w.Write((uint)mesh.subMeshCount);
            foreach (var p in v) { w.Write(p.x); w.Write(p.y); w.Write(-p.z); }
            if ((flags & 1) != 0) foreach (var p in nrm) { w.Write(p.x); w.Write(p.y); w.Write(-p.z); }
            if ((flags & 2) != 0) foreach (var p in uv) { w.Write(p.x); w.Write(1f - p.y); }
            if ((flags & 4) != 0) foreach (var p in uv2) { w.Write(p.x); w.Write(1f - p.y); }
            if ((flags & 8) != 0) foreach (var c in col) { w.Write(c.r); w.Write(c.g); w.Write(c.b); w.Write(c.a); }
            for (int s = 0; s < mesh.subMeshCount; s++)
            {
                int[] tris = mesh.GetTriangles(s);
                w.Write((uint)tris.Length);
                for (int i = 0; i + 2 < tris.Length; i += 3)
                {
                    // Mirroring Z turns the triangles inside out: swap two corners.
                    w.Write((uint)tris[i]);
                    w.Write((uint)tris[i + 2]);
                    w.Write((uint)tris[i + 1]);
                }
            }
        }
    }

    static Json TerrainKey(TerrainData td)
    {
        if (td == null)
            return Json.Null();
        var id = td;
        if (terrainKeys.TryGetValue(id, out var key))
            return Json.Str(key);
        key = UniqueName("terrain:", td.name);
        terrainKeys[id] = key;

        var j = Json.Obj();
        j["asset"] = AssetDatabase.GetAssetPath(td);
        j["size"] = Vec(td.size);
        int res = td.heightmapResolution;
        j["resolution"] = res;
        j["heights_file"] = "terrain/" + key + ".r32";

        // Unity's row index runs along +Z; Godot's -Z is Unity's +Z, so the last Unity row
        // is written first.
        float[,] h = td.GetHeights(0, 0, res, res);
        float lo = float.MaxValue, hi = float.MinValue;
        using (var w = new BinaryWriter(File.Create(Path.Combine(outDir, "terrain", key + ".r32"))))
        {
            for (int z = res - 1; z >= 0; z--)
                for (int x = 0; x < res; x++)
                {
                    float v = h[z, x];
                    lo = Math.Min(lo, v);
                    hi = Math.Max(hi, v);
                    w.Write(v);
                }
        }
        j["min_height"] = lo;
        j["max_height"] = hi;

        // Layers and the mix of their colours.
        var layers = Json.Arr();
        var layerColors = new List<Color>();
        foreach (var layer in td.terrainLayers)
        {
            var lj = Json.Obj();
            if (layer == null)
            {
                layers.Add(Json.Null());
                layerColors.Add(Color.grey);
                continue;
            }
            lj["name"] = layer.name;
            lj["texture"] = TextureKey(layer.diffuseTexture);
            lj["tile_size"] = Vec(layer.tileSize);
            lj["tint"] = Col(layer.diffuseRemapMax);
            Color avg = AverageColor(layer.diffuseTexture);
            Color tint = layer.diffuseRemapMax;
            avg = new Color(avg.r * tint.r, avg.g * tint.g, avg.b * tint.b, 1f);
            lj["average_color"] = Col(avg);
            layerColors.Add(avg);
            layers.Add(lj);
        }
        j["layers"] = layers;

        int aw = td.alphamapWidth, ah = td.alphamapHeight;
        if (td.alphamapLayers > 0 && aw > 0 && ah > 0)
        {
            float[,,] a = td.GetAlphamaps(0, 0, aw, ah);
            var img = new Texture2D(aw, ah, TextureFormat.RGBA32, false);
            var weights = new float[td.alphamapLayers];
            for (int z = 0; z < ah; z++)
                for (int x = 0; x < aw; x++)
                {
                    Color c = Color.black;
                    for (int l = 0; l < td.alphamapLayers; l++)
                    {
                        float wgt = a[z, x, l];
                        weights[l] += wgt;
                        if (l < layerColors.Count)
                            c += layerColors[l] * wgt;
                    }
                    c.a = 1f;
                    // Texture row 0 is the bottom of the PNG, Unity's z = 0, which is the
                    // near (Godot +Z) side: the same orientation as the heights.
                    img.SetPixel(x, z, c);
                }
            img.Apply();
            j["color_file"] = "terrain/" + key + "_color.png";
            File.WriteAllBytes(Path.Combine(outDir, "terrain", key + "_color.png"), img.EncodeToPNG());
            var wj = Json.Arr();
            foreach (var wgt in weights)
                wj.Add(wgt / (aw * ah));
            j["layer_weights"] = wj;
        }

        // Trees, in the terrain's own space, in metres.
        var protos = Json.Arr();
        foreach (var tp in td.treePrototypes)
            protos.Add(tp.prefab != null ? Json.Str(PrefabKey(tp.prefab)) : Json.Null());
        j["tree_prototypes"] = protos;
        var trees = Json.Arr();
        foreach (var tree in td.treeInstances)
        {
            Vector3 p = Vector3.Scale(tree.position, td.size);
            trees.Add(Json.Arr(tree.prototypeIndex, p.x, p.y, p.z, tree.rotation,
                tree.widthScale, tree.heightScale));
        }
        j["trees"] = trees;
        j["detail_prototypes"] = td.detailPrototypes.Length;
        terrains[key] = j;
        return Json.Str(key);
    }

    static Color AverageColor(Texture2D tex)
    {
        var small = ReadPixels(tex, 32, 32);
        if (small == null)
            return Color.grey;
        Color sum = Color.black;
        foreach (var c in small.GetPixels())
            sum += c;
        return sum / (32 * 32);
    }

    static Json RenderSettingsJson()
    {
        var j = Json.Obj();
        j["ambient_mode"] = RenderSettings.ambientMode.ToString();
        j["ambient_light"] = Col(RenderSettings.ambientLight);
        j["ambient_sky"] = Col(RenderSettings.ambientSkyColor);
        j["ambient_equator"] = Col(RenderSettings.ambientEquatorColor);
        j["ambient_ground"] = Col(RenderSettings.ambientGroundColor);
        j["ambient_intensity"] = RenderSettings.ambientIntensity;
        j["fog"] = RenderSettings.fog;
        j["fog_color"] = Col(RenderSettings.fogColor);
        j["fog_mode"] = RenderSettings.fogMode.ToString();
        j["fog_density"] = RenderSettings.fogDensity;
        j["fog_start"] = RenderSettings.fogStartDistance;
        j["fog_end"] = RenderSettings.fogEndDistance;
        j["skybox"] = MaterialKey(RenderSettings.skybox);
        if (RenderSettings.sun != null)
            j["sun"] = RenderSettings.sun.name;
        return j;
    }

    static void Warn(string msg)
    {
        log.AppendLine(msg);
        Debug.LogWarning("RetroSceneExport: " + msg);
    }

    // ------------------------------------------------------------------------------------
    // Values in Godot's axes

    static Json Pos(Vector3 v) => Json.Arr(v.x, v.y, -v.z);
    static Json Rot(Quaternion q) => Json.Arr(-q.x, -q.y, q.z, q.w);
    static Json Vec(Vector2 v) => Json.Arr(v.x, v.y);
    static Json Vec(Vector3 v) => Json.Arr(v.x, v.y, v.z);
    static Json Vec(Vector4 v) => Json.Arr(v.x, v.y, v.z, v.w);
    static Json Col(Color c) => Json.Arr(c.r, c.g, c.b, c.a);

    // ------------------------------------------------------------------------------------
    // A small JSON writer, so the script needs no package.

    class Json
    {
        enum Kind { Null, Bool, Num, Str, Arr, Obj }
        Kind kind;
        bool b;
        double num;
        string str;
        List<Json> arr;
        List<KeyValuePair<string, Json>> obj;

        public static Json Null() => new Json { kind = Kind.Null };
        public static Json Bool(bool v) => new Json { kind = Kind.Bool, b = v };
        public static Json Num(double v) => new Json { kind = Kind.Num, num = v };
        public static Json Str(string v) => v == null ? Null() : new Json { kind = Kind.Str, str = v };
        public static Json Obj() => new Json { kind = Kind.Obj, obj = new List<KeyValuePair<string, Json>>() };
        public static Json Arr(params float[] v)
        {
            var j = new Json { kind = Kind.Arr, arr = new List<Json>() };
            foreach (var x in v) j.arr.Add(Num(x));
            return j;
        }
        public static Json Arr(string[] v)
        {
            var j = new Json { kind = Kind.Arr, arr = new List<Json>() };
            foreach (var x in v) j.arr.Add(Str(x));
            return j;
        }
        public static Json Arr() => new Json { kind = Kind.Arr, arr = new List<Json>() };
        public static Json Arr(int a, float b, float c, float d, float e, float f, float g)
        {
            var j = Arr();
            foreach (var x in new double[] { a, b, c, d, e, f, g }) j.arr.Add(Num(x));
            return j;
        }

        public static implicit operator Json(string v) => Str(v);
        public static implicit operator Json(bool v) => Bool(v);
        public static implicit operator Json(float v) => Num(v);
        public static implicit operator Json(int v) => Num(v);

        public void Add(Json v) => arr.Add(v);
        public Json Set(string k, Json v) { this[k] = v; return this; }

        public Json this[string k]
        {
            set
            {
                for (int i = 0; i < obj.Count; i++)
                    if (obj[i].Key == k) { obj[i] = new KeyValuePair<string, Json>(k, value); return; }
                obj.Add(new KeyValuePair<string, Json>(k, value));
            }
        }

        public override string ToString()
        {
            var sb = new StringBuilder();
            Write(sb, 0);
            return sb.ToString();
        }

        void Write(StringBuilder sb, int indent)
        {
            switch (kind)
            {
                case Kind.Null: sb.Append("null"); break;
                case Kind.Bool: sb.Append(b ? "true" : "false"); break;
                case Kind.Num:
                    if (double.IsNaN(num) || double.IsInfinity(num)) sb.Append("0");
                    else sb.Append(num.ToString("R", CultureInfo.InvariantCulture));
                    break;
                case Kind.Str: Quote(sb, str); break;
                case Kind.Arr:
                    {
                        bool flat = arr.TrueForAll(x => x.kind == Kind.Num || x.kind == Kind.Bool || x.kind == Kind.Null);
                        sb.Append('[');
                        for (int i = 0; i < arr.Count; i++)
                        {
                            if (i > 0) sb.Append(flat ? ", " : ",");
                            if (!flat) { sb.Append('\n'); sb.Append(' ', indent + 2); }
                            arr[i].Write(sb, indent + 2);
                        }
                        if (!flat && arr.Count > 0) { sb.Append('\n'); sb.Append(' ', indent); }
                        sb.Append(']');
                        break;
                    }
                case Kind.Obj:
                    sb.Append('{');
                    for (int i = 0; i < obj.Count; i++)
                    {
                        if (i > 0) sb.Append(',');
                        sb.Append('\n'); sb.Append(' ', indent + 2);
                        Quote(sb, obj[i].Key);
                        sb.Append(": ");
                        obj[i].Value.Write(sb, indent + 2);
                    }
                    if (obj.Count > 0) { sb.Append('\n'); sb.Append(' ', indent); }
                    sb.Append('}');
                    break;
            }
        }

        static void Quote(StringBuilder sb, string s)
        {
            sb.Append('"');
            foreach (char c in s)
            {
                switch (c)
                {
                    case '"': sb.Append("\\\""); break;
                    case '\\': sb.Append("\\\\"); break;
                    case '\n': sb.Append("\\n"); break;
                    case '\r': sb.Append("\\r"); break;
                    case '\t': sb.Append("\\t"); break;
                    default:
                        if (c < 0x20) sb.AppendFormat("\\u{0:x4}", (int)c);
                        else sb.Append(c);
                        break;
                }
            }
            sb.Append('"');
        }
    }
}
