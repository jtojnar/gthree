#include <math.h>
#include <epoxy/gl.h>

#include "gthreeshader.h"
#include "gthreeprogram.h"
#include "gthreeprivate.h"


typedef struct {
  char *name;
  GPtrArray *defines;
  GthreeUniforms *uniforms;
  char *vertex_shader_text;
  char *fragment_shader_text;
  GthreeShader *owner_of_shader_text;
  guint hash;
} GthreeShaderPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GthreeShader, gthree_shader, G_TYPE_OBJECT);

static void gthree_shader_init_libs ();
static void gthree_shader_init_hash (GthreeShader *shader);

GthreeShader *
gthree_shader_new (GPtrArray *defines,
                   GthreeUniforms *uniforms,
                   const char *vertex_shader_text,
                   const char *fragment_shader_text)
{
  GthreeShader *shader;
  GthreeShaderPrivate *priv;

  shader = g_object_new (gthree_shader_get_type (), NULL);
  priv = gthree_shader_get_instance_private (shader);

  if (defines)
    priv->defines = g_ptr_array_ref (defines);
  if (uniforms)
    priv->uniforms = g_object_ref (uniforms);

  if (vertex_shader_text)
    priv->vertex_shader_text = g_strdup (vertex_shader_text);
  else
    priv->vertex_shader_text = g_strdup ("void main() {\n\tgl_Position = projectionMatrix * modelViewMatrix * vec4( position, 1.0 );\n}");

  if (fragment_shader_text)
    priv->fragment_shader_text = g_strdup (fragment_shader_text);
  else
    priv->fragment_shader_text = g_strdup ("void main() {\n\tgl_FragColor = vec4( 1.0, 0.0, 0.0, 1.0 );\n}");

  priv->owner_of_shader_text = NULL;

  gthree_shader_init_hash (shader);

  return shader;
}

static void
gthree_shader_init (GthreeShader *shader)
{
  GthreeShaderPrivate *priv = gthree_shader_get_instance_private (shader);

  priv->owner_of_shader_text = NULL;
}

static void
gthree_shader_finalize (GObject *obj)
{
  GthreeShader *shader = GTHREE_SHADER (obj);
  GthreeShaderPrivate *priv = gthree_shader_get_instance_private (shader);

  g_free (priv->name);
  g_clear_pointer (&priv->defines, g_ptr_array_unref);
  g_clear_object (&priv->uniforms);

  if (priv->owner_of_shader_text)
    g_object_unref (priv->owner_of_shader_text);
  else
    {
      g_clear_pointer (&priv->vertex_shader_text, g_free);
      g_clear_pointer (&priv->fragment_shader_text, g_free);
    }

  G_OBJECT_CLASS (gthree_shader_parent_class)->finalize (obj);
}

static void
gthree_shader_class_init (GthreeShaderClass *klass)
{
  G_OBJECT_CLASS (klass)->finalize = gthree_shader_finalize;

  gthree_shader_init_libs ();
}

static gboolean
str_equal (const char *a, const char *b)
{
  if (a == b)
    return TRUE;

  return strcmp (a, b) == 0;
}

static gboolean
defines_equal (GPtrArray *a, GPtrArray *b)
{
  int i;
  int a_len, b_len;

  if (a == b)
    return TRUE;

  a_len = a ? a->len : 0;
  b_len = b ? b->len : 0;

  if (a_len == 0)
    return b_len == 0;
  if (b_len == 0)
    return a_len == 0;

  if (a_len != b_len)
    return FALSE;

  for (i = 0; i < a_len; i++)
    {
      if (!str_equal (g_ptr_array_index (a, i), g_ptr_array_index (b, i)))
        return FALSE;
    }

  return TRUE;
}

static guint
defines_hash (GPtrArray *a)
{
  guint hash = 0;
  int i;

  if (a != NULL)
    {
      for (i = 0; i < a->len; i++)
        hash ^= g_str_hash (g_ptr_array_index (a, i));
    }

  return hash;
}

gboolean
gthree_shader_equal (GthreeShader *a,
                     GthreeShader *b)
{
  GthreeShaderPrivate *priv_a = gthree_shader_get_instance_private (a);
  GthreeShaderPrivate *priv_b = gthree_shader_get_instance_private (b);

  if (a == b)
    return TRUE;

  return
    defines_equal (priv_a->defines, priv_b->defines) &&
    str_equal (priv_a->vertex_shader_text, priv_b->vertex_shader_text) &&
    str_equal (priv_a->fragment_shader_text, priv_b->fragment_shader_text);
}

static void
gthree_shader_init_hash (GthreeShader *shader)
{
  GthreeShaderPrivate *priv = gthree_shader_get_instance_private (shader);

  priv->hash =
    defines_hash (priv->defines) ^
    g_str_hash (priv->vertex_shader_text) ^
    g_str_hash (priv->fragment_shader_text);
}

guint
gthree_shader_hash (GthreeShader *shader)
{
  GthreeShaderPrivate *priv = gthree_shader_get_instance_private (shader);

  return priv->hash;
}

void
gthree_shader_set_name (GthreeShader *shader,
                        const char *name)
{
  GthreeShaderPrivate *priv = gthree_shader_get_instance_private (shader);

  g_free (priv->name);
  priv->name = g_strdup (name);
}

const char *
gthree_shader_get_name (GthreeShader *shader)
{
  GthreeShaderPrivate *priv = gthree_shader_get_instance_private (shader);

  return priv->name;
}

void
gthree_shader_update_uniform_locations_for_program (GthreeShader *shader,
                                                    GthreeProgram *program)
{
  GthreeShaderPrivate *priv = gthree_shader_get_instance_private (shader);
  GList *unis, *l, *ll;

  unis = gthree_uniforms_get_all (priv->uniforms);
  for (l = unis; l != NULL; l = l->next)
    {
      GthreeUniform *uni = l->data;
      GthreeUniformType uni_type = gthree_uniform_get_type (uni);
      gint location;

      if (uni_type == GTHREE_UNIFORM_TYPE_UNIFORMS_ARRAY)
        {
          GPtrArray *uarray = gthree_uniform_get_uarray (uni);
          int i;

          for (i = 0; uarray && i < uarray->len; i++)
            {
              GthreeUniforms *child_unis = g_ptr_array_index (uarray, i);
              GList *child_unis_list;
              const char *child_name;
              g_autofree char *full_name = NULL;

              if (child_unis == NULL)
                continue;

              child_unis_list = gthree_uniforms_get_all (child_unis);

              for (ll = child_unis_list; ll != NULL; ll = ll->next)
                {
                  GthreeUniform *child_uni = ll->data;

                  child_name = gthree_uniform_get_name (child_uni);

                  full_name = g_strdup_printf ("%s[%d].%s",
                                               gthree_uniform_get_name (uni), i, child_name);

                  location = gthree_program_lookup_uniform_location_from_string (program, full_name);
                  gthree_uniform_set_location (child_uni, location);
                }
            }
        }
      else
        {
          const char *u_name;
          g_autofree char *real_name = NULL;

          u_name = gthree_uniform_get_name (uni);
          if (gthree_uniform_is_array (uni))
            {
              real_name = g_strdup_printf ("%s[0]", u_name);
              u_name = real_name;
            }

          location = gthree_program_lookup_uniform_location_from_string (program, u_name);
          gthree_uniform_set_location (uni, location);
        }
    }
  g_list_free (unis);
}

GPtrArray *
gthree_shader_get_defines (GthreeShader  *shader)
{
  GthreeShaderPrivate *priv = gthree_shader_get_instance_private (shader);

  return priv->defines;
}

void
gthree_shader_set_defines (GthreeShader *shader,
                           GPtrArray *defines)
{
  GthreeShaderPrivate *priv = gthree_shader_get_instance_private (shader);

  if (defines)
    g_ptr_array_ref (defines);

  if (priv->defines)
    g_ptr_array_unref (priv->defines);

  priv->defines = defines;

  gthree_shader_init_hash (shader);

}

GthreeUniforms *
gthree_shader_get_uniforms (GthreeShader  *shader)
{
  GthreeShaderPrivate *priv = gthree_shader_get_instance_private (shader);

  return priv->uniforms;
}

const char *
gthree_shader_get_vertex_shader_text (GthreeShader  *shader)
{
  GthreeShaderPrivate *priv = gthree_shader_get_instance_private (shader);

  return priv->vertex_shader_text;
}

const char *
gthree_shader_get_fragment_shader_text (GthreeShader  *shader)
{
  GthreeShaderPrivate *priv = gthree_shader_get_instance_private (shader);

  return priv->fragment_shader_text;
}

GthreeShader *
gthree_shader_clone (GthreeShader *orig)
{
  GthreeShader *clone;
  GthreeShaderPrivate *orig_priv, *clone_priv;
  int i;

  orig_priv = gthree_shader_get_instance_private (orig);

  clone = g_object_new (gthree_shader_get_type (), NULL);
  clone_priv = gthree_shader_get_instance_private (clone);

  clone_priv->name = g_strdup (orig_priv->name);
  clone_priv->uniforms = gthree_uniforms_clone (orig_priv->uniforms);
  clone_priv->vertex_shader_text = orig_priv->vertex_shader_text;
  clone_priv->fragment_shader_text = orig_priv->fragment_shader_text;

  if (orig_priv->defines)
    {
      clone_priv->defines = g_ptr_array_new_with_free_func (g_free);
      for (i = 0; i < orig_priv->defines->len; i++)
        g_ptr_array_add (clone_priv->defines, g_strdup (g_ptr_array_index (orig_priv->defines, i)));
    }

  clone_priv->hash = orig_priv->hash;

  if (orig_priv->owner_of_shader_text)
    clone_priv->owner_of_shader_text = g_object_ref (orig_priv->owner_of_shader_text);
  else
    clone_priv->owner_of_shader_text = g_object_ref (orig);

  return clone;
}

static void
parse_include (char *file,
               GString *s)
{
  GBytes *bytes;
  char *end;
  g_autofree char *full_path = NULL;

  while (*file != '<' && *file != 0)
    file++;

  if (*file == 0)
    {
      g_warning ("No initial \" in include");
      return;
    }

  file++;

  end = file;
  while (*end != '>' && *end != 0)
    end++;

  if (*end == 0)
    {
      g_warning ("No final \" in include");
      return;
    }

  *end = 0;

  full_path = g_strconcat ("/org/gnome/gthree/shader_chunks/", file, ".glsl", NULL);
  bytes = g_resources_lookup_data (full_path, 0, NULL);
  if (bytes == NULL)
    {
      g_warning ("shader snipped %s not found", file);
      return;
    }

  g_string_append_printf (s, "// Include: %s\n", file);
  g_string_append_len (s,
                        g_bytes_get_data (bytes, NULL),
                        g_bytes_get_size (bytes));
  g_string_append_c (s, '\n');
  g_bytes_unref (bytes);
}

static char *
parse_text_with_includes (const char *text)
{
  GString *s;
  char **lines;
  int i;

  s = g_string_new ("");

  lines = g_strsplit (text, "\n", -1);
  for (i = 0; lines[i] != NULL; i++)
    {
      char *line = lines[i];
      while (g_ascii_isspace (*line))
        line++;
      if (g_str_has_prefix (line, "#include"))
        parse_include (lines[i] + strlen ("#include"), s);
      else
        {
          g_string_append (s, lines[i]);
          g_string_append_c (s, '\n');
        }
    }

  g_strfreev (lines);

  return g_string_free (s, FALSE);
}

GthreeShader *
gthree_shader_new_from_definitions (const char **lib_uniforms,
                                    GthreeUniformsDefinition *uniform_defs, int defs_len,
                                    const char **defines,
                                    const char *vertex_shader_name,
                                    const char *fragment_shader_name)
{
  GthreeShader *shader = g_object_new (gthree_shader_get_type (), NULL);
  GthreeShaderPrivate *priv = gthree_shader_get_instance_private (shader);
  GthreeUniforms *uniforms;
  g_autofree char *vertex_path = NULL;
  g_autofree char *fragment_path = NULL;
  g_autoptr(GBytes) vertex_bytes = NULL;
  g_autoptr(GBytes) fragment_bytes = NULL;
  int i;

  priv->uniforms = gthree_uniforms_new ();
  for (i = 0; lib_uniforms[i] != NULL; i++)
    {
      uniforms = gthree_get_uniforms_from_library (lib_uniforms[i]);
      gthree_uniforms_merge (priv->uniforms, uniforms);
    }

  if (uniform_defs)
    {
      uniforms = gthree_uniforms_new_from_definitions (uniform_defs, defs_len);
      gthree_uniforms_merge (priv->uniforms, uniforms);
      g_object_unref (uniforms);
    }

  vertex_path = g_strconcat ("/org/gnome/gthree/shader_lib/", vertex_shader_name, ".glsl", NULL);
  vertex_bytes = g_resources_lookup_data (vertex_path, 0, NULL);
  g_assert (vertex_bytes != NULL);
  priv->vertex_shader_text = parse_text_with_includes (g_bytes_get_data (vertex_bytes, NULL));
  g_assert (priv->vertex_shader_text != NULL);

  fragment_path = g_strconcat ("/org/gnome/gthree/shader_lib/", fragment_shader_name, ".glsl", NULL);
  fragment_bytes = g_resources_lookup_data (fragment_path, 0, NULL);
  g_assert (fragment_bytes != NULL);
  priv->fragment_shader_text = parse_text_with_includes (g_bytes_get_data (fragment_bytes, NULL));
  g_assert (priv->fragment_shader_text != NULL);

  if (defines)
    {
      priv->defines = g_ptr_array_new_with_free_func (g_free);
      for (i = 0; defines[i] != NULL; i++)
        g_ptr_array_add (priv->defines, g_strdup (defines[i]));
      g_assert (i % 2 == 0); // Must be (key, value) * n
    }

  gthree_shader_init_hash (shader);

  return shader;
}

static float f0 = 0.0;
static float f1 = 1.0;
static float f2 = 2.0;
static float f30 = 30;
static float fm1 = -1.0;
static float f1000 = 1000;
static float fp5 = 0.5;
static float dark_grey[3] = { 0.06666666666666667, 0.06666666666666667, 0.06666666666666667 };
static float black[3] = { 0, 0, 0 };
static float zerov3[3] = { 0, 0, 0 };
static float one_matrix3[9] = { 1, 0, 0,
                                0, 1, 0,
                                0, 0, 1};

static const char *basic_uniform_libs[] = { "common", "specularmap", "envmap", "aomap", "lightmap", "fog", NULL };
static const char *lambert_uniform_libs[] = { "common", "specularmap", "envmap", "aomap", "lightmap", "emissivemap", "fog", "lights", NULL };
static GthreeUniformsDefinition lambert_uniforms[] = {
  {"emissive", GTHREE_UNIFORM_TYPE_VECTOR3, &black },
};

static const char *phong_uniform_libs[] = { "common", "specularmap", "envmap", "aomap", "lightmap", "emissivemap", "bumpmap", "normalmap", "displacementmap", "gradientmap", "fog", "lights", NULL };
static GthreeUniformsDefinition phong_uniforms[] = {
  {"emissive", GTHREE_UNIFORM_TYPE_VECTOR3, &black },
  {"specular", GTHREE_UNIFORM_TYPE_VECTOR3, &dark_grey },
  {"shininess", GTHREE_UNIFORM_TYPE_FLOAT, &f30 },
};

static const char *standard_uniform_libs[] = { "common", "envmap", "aomap", "lightmap", "emissivemap", "bumpmap", "normalmap", "displacementmap", "roughnessmap", "metalnessmap", "fog", "lights", NULL };
static GthreeUniformsDefinition standard_uniforms[] = {
  {"emissive", GTHREE_UNIFORM_TYPE_VECTOR3, &black },
  {"roughness", GTHREE_UNIFORM_TYPE_FLOAT, &fp5 },
  {"metalness", GTHREE_UNIFORM_TYPE_FLOAT, &fp5 },
  {"envMapIntensity", GTHREE_UNIFORM_TYPE_FLOAT, &f1 },
};

static const char *matcap_uniform_libs[] = { "common", "bumpmap", "normalmap", "displacementmap", "fog", NULL };
static GthreeUniformsDefinition matcap_uniforms[] = {
  {"emissive", GTHREE_UNIFORM_TYPE_TEXTURE, NULL },
};

static const char *points_uniform_libs[] = { "points", "fog", NULL };

static const char *dashed_uniform_libs[] = { "common", "fog", NULL };
static GthreeUniformsDefinition dashed_uniforms[] = {
  {"scale", GTHREE_UNIFORM_TYPE_FLOAT, &f1 },
  {"dashSize", GTHREE_UNIFORM_TYPE_FLOAT, &f1 },
  {"totalSize", GTHREE_UNIFORM_TYPE_FLOAT, &f2 },
};

static const char *depth_uniform_libs[] = { "common", "displacementmap", NULL };

static const char *normal_uniform_libs[] = { "common", "bumpmap", "normalmap", "displacementmap", NULL };
static GthreeUniformsDefinition normal_uniforms[] = {
  {"opacity", GTHREE_UNIFORM_TYPE_FLOAT, &f1 },
};

static const char *sprite_uniform_libs[] = { "sprite", "fog", NULL };

static const char *background_uniform_libs[] = { NULL };
static GthreeUniformsDefinition background_uniforms[] = {
  {"uvTransform", GTHREE_UNIFORM_TYPE_MATRIX3, &one_matrix3},
  {"t2D", GTHREE_UNIFORM_TYPE_TEXTURE, NULL},
};

static const char *cube_uniform_libs[] = { NULL };
static GthreeUniformsDefinition cube_uniforms[] = {
  {"tCube", GTHREE_UNIFORM_TYPE_TEXTURE, NULL},
  {"tFlip", GTHREE_UNIFORM_TYPE_FLOAT, &fm1},
  {"opacity", GTHREE_UNIFORM_TYPE_FLOAT, &f1 },
};

static const char *equirect_uniform_libs[] = { NULL };
static GthreeUniformsDefinition equirect_uniforms[] = {
  {"tEquirect", GTHREE_UNIFORM_TYPE_TEXTURE, NULL},
};

static const char *distanceRGBA_uniform_libs[] = { "common", "displacementmap", NULL };
static GthreeUniformsDefinition distanceRGBA_uniforms[] = {
  {"referencePosition", GTHREE_UNIFORM_TYPE_VECTOR3, &zerov3},
  {"nearDistance", GTHREE_UNIFORM_TYPE_FLOAT, &f1 },
  {"farDistance", GTHREE_UNIFORM_TYPE_FLOAT, &f1000 },
};

static const char *shadow_uniform_libs[] = { "lights", "fog", NULL };
static GthreeUniformsDefinition shadow_uniforms[] = {
  {"color", GTHREE_UNIFORM_TYPE_VECTOR3, &black },
  {"opacity", GTHREE_UNIFORM_TYPE_FLOAT, &f1 },
};

static const char *physical_uniform_libs[] = { "common", "envmap", "aomap", "lightmap", "emissivemap", "bumpmap", "normalmap", "displacementmap", "roughnessmap", "metalnessmap", "fog", "lights", NULL };
static GthreeUniformsDefinition physical_uniforms[] = {
  {"clearCoat", GTHREE_UNIFORM_TYPE_FLOAT, &f0 },
  {"clearCoatRoughness", GTHREE_UNIFORM_TYPE_FLOAT, &f0 },
};

static const char *copy_uniform_libs[] = { NULL };
static GthreeUniformsDefinition copy_uniforms[] = {
  {"tDiffuse", GTHREE_UNIFORM_TYPE_TEXTURE, NULL},
  {"opacity", GTHREE_UNIFORM_TYPE_FLOAT, &f1 },
};

static float convolution_default_increment[2] = { 0.001953125, 0.0 };
static const char *convolution_uniform_libs[] = { NULL };
static GthreeUniformsDefinition convolution_uniforms[] = {
  {"tDiffuse", GTHREE_UNIFORM_TYPE_TEXTURE, NULL},
  {"uImageIncrement", GTHREE_UNIFORM_TYPE_VECTOR2, &convolution_default_increment},
  {"cKernel", GTHREE_UNIFORM_TYPE_FLOAT_ARRAY, NULL },
};
static const char *convolution_defines[] = {
  "KERNEL_SIZE_FLOAT", "25.0",
  "KERNEL_SIZE_INT", "25",
  NULL
};

static GthreeShader *basic, *lambert, *phong, *standard, *matcap, *points, *dashed, *depth, *normal, *sprite, *background;
static GthreeShader *cube, *equirect, *distanceRGBA, *shadow, *physical, *copy, *convolution;

static void
gthree_shader_init_libs ()
{
  static gboolean initialized = FALSE;

  if (initialized)
    return;

  basic = gthree_shader_new_from_definitions (basic_uniform_libs,
                                              NULL, 0,
                                              NULL,
                                              "meshbasic_vert", "meshbasic_frag");
  gthree_shader_set_name (basic, "basic");

  lambert = gthree_shader_new_from_definitions (lambert_uniform_libs,
                                                lambert_uniforms, G_N_ELEMENTS (lambert_uniforms),
                                                NULL,
                                                "meshlambert_vert", "meshlambert_frag");
  gthree_shader_set_name (lambert, "lambert");

  phong = gthree_shader_new_from_definitions (phong_uniform_libs,
                                              phong_uniforms, G_N_ELEMENTS (phong_uniforms),
                                              NULL,
                                              "meshphong_vert", "meshphong_frag");
  gthree_shader_set_name (phong, "phong");

  standard = gthree_shader_new_from_definitions (standard_uniform_libs,
                                                 standard_uniforms, G_N_ELEMENTS (standard_uniforms),
                                                 NULL,
                                                 "meshphysical_vert", "meshphysical_frag");
  gthree_shader_set_name (standard, "standard");

  matcap = gthree_shader_new_from_definitions (matcap_uniform_libs,
                                               matcap_uniforms, G_N_ELEMENTS (matcap_uniforms),
                                               NULL,
                                               "meshmatcap_vert", "meshmatcap_frag");
  gthree_shader_set_name (matcap, "matcap");

  points = gthree_shader_new_from_definitions (points_uniform_libs,
                                               NULL, 0,
                                               NULL,
                                              "points_vert", "points_frag");
  gthree_shader_set_name (points, "points");

  dashed = gthree_shader_new_from_definitions (dashed_uniform_libs,
                                               dashed_uniforms, G_N_ELEMENTS (dashed_uniforms),
                                               NULL,
                                               "linedashed_vert", "linedashed_frag");
  gthree_shader_set_name (dashed, "dashed");

  depth = gthree_shader_new_from_definitions (depth_uniform_libs,
                                              NULL, 0,
                                              NULL,
                                              "depth_vert", "depth_frag");
  gthree_shader_set_name (depth, "depth");

  normal = gthree_shader_new_from_definitions (normal_uniform_libs,
                                               normal_uniforms, G_N_ELEMENTS (normal_uniforms),
                                               NULL,
                                               "normal_vert", "normal_frag");
  gthree_shader_set_name (normal, "normal");

  sprite = gthree_shader_new_from_definitions (sprite_uniform_libs,
                                               NULL, 0,
                                               NULL,
                                              "sprite_vert", "sprite_frag");
  gthree_shader_set_name (sprite, "sprite");

  background = gthree_shader_new_from_definitions (background_uniform_libs,
                                                   background_uniforms, G_N_ELEMENTS (background_uniforms),
                                                   NULL,
                                                   "background_vert", "background_frag");
  gthree_shader_set_name (background, "background");

  cube = gthree_shader_new_from_definitions (cube_uniform_libs,
                                             cube_uniforms, G_N_ELEMENTS (cube_uniforms),
                                             NULL,
                                             "cube_vert", "cube_frag");
  gthree_shader_set_name (cube, "cube");

  equirect = gthree_shader_new_from_definitions (equirect_uniform_libs,
                                                 equirect_uniforms, G_N_ELEMENTS (equirect_uniforms),
                                                 NULL,
                                                 "equirect_vert", "equirect_frag");
  gthree_shader_set_name (equirect, "equirect");

  distanceRGBA = gthree_shader_new_from_definitions (distanceRGBA_uniform_libs,
                                                     distanceRGBA_uniforms, G_N_ELEMENTS (distanceRGBA_uniforms),
                                                     NULL,
                                                     "distanceRGBA_vert", "distanceRGBA_frag");
  gthree_shader_set_name (distanceRGBA, "distanceRGBA");

  shadow = gthree_shader_new_from_definitions (shadow_uniform_libs,
                                               shadow_uniforms, G_N_ELEMENTS (shadow_uniforms),
                                               NULL,
                                               "shadow_vert", "shadow_frag");
  gthree_shader_set_name (shadow, "shadow");

  physical = gthree_shader_new_from_definitions (physical_uniform_libs,
                                                 physical_uniforms, G_N_ELEMENTS (physical_uniforms),
                                                 NULL,
                                                 "meshphysical_vert", "meshphysical_frag");
  gthree_shader_set_name (physical, "physical");

  copy = gthree_shader_new_from_definitions (copy_uniform_libs,
                                             copy_uniforms, G_N_ELEMENTS (copy_uniforms),
                                             NULL,
                                             "copy_vert", "copy_frag");
  gthree_shader_set_name (copy, "copy");

  convolution = gthree_shader_new_from_definitions (convolution_uniform_libs,
                                                    convolution_uniforms, G_N_ELEMENTS (convolution_uniforms),
                                                    convolution_defines,
                                                    "convolution_vert", "convolution_frag");
  gthree_shader_set_name (convolution, "convolution");

  initialized = TRUE;
}

GthreeShader *
gthree_get_shader_from_library (const char *name)
{
  gthree_shader_init_libs ();

  if (strcmp (name, "basic") == 0)
    return basic;

  if (strcmp (name, "lambert") == 0)
    return lambert;

  if (strcmp (name, "phong") == 0)
    return phong;

  if (strcmp (name, "standard") == 0)
    return standard;

  if (strcmp (name, "matcap") == 0)
    return matcap;

  if (strcmp (name, "points") == 0)
    return points;

  if (strcmp (name, "dashed") == 0)
    return dashed;

  if (strcmp (name, "depth") == 0)
    return depth;

  if (strcmp (name, "normal") == 0)
    return normal;

  if (strcmp (name, "sprite") == 0)
    return sprite;

  if (strcmp (name, "background") == 0)
    return background;

  if (strcmp (name, "cube") == 0)
    return cube;

  if (strcmp (name, "equirect") == 0)
    return equirect;

  if (strcmp (name, "distanceRGBA") == 0)
    return distanceRGBA;

  if (strcmp (name, "shadow") == 0)
    return shadow;

  if (strcmp (name, "physical") == 0)
    return physical;

  if (strcmp (name, "copy") == 0)
    return copy;

  if (strcmp (name, "convolution") == 0)
    return convolution;

  g_warning ("can't find shader library %s\n", name);
  return NULL;
}

GthreeShader *
gthree_clone_shader_from_library (const char *name)
{
  GthreeShader *orig;

  orig = gthree_get_shader_from_library (name);
  if (orig == NULL)
    return NULL;

  return gthree_shader_clone (orig);
}

// We lop off the sqrt(2 * pi) * sigma term, since we're going to normalize anyway.
static float
gauss (float x, float sigma)
{
  return expf( - (x * x) / (2.0 * sigma * sigma));
}

GArray *
gthree_convolution_shader_build_kernel (float sigma)
{
  GArray *values;
  int i, kMaxKernelSize, kernelSize;
  float sum, halfWidth;

  kMaxKernelSize = 25;
  kernelSize = 2 * ceilf (sigma * 3.0) + 1;

  if (kernelSize > kMaxKernelSize)
    kernelSize = kMaxKernelSize;

  halfWidth = (kernelSize - 1) * 0.5;

  values = g_array_new (FALSE, TRUE, sizeof (float));
  sum = 0.0;
  for (i = 0; i < kernelSize; i++)
    {
      float value = gauss (i - halfWidth, sigma);
      sum += value;
      g_array_append_val (values, value);
  }

  // normalize the kernel
  for (i = 0; i < kernelSize; i++)
    g_array_index(values, float, i) /= sum;

  return values;
}
