#include <math.h>
#include <epoxy/gl.h>

#include "gthreemesh.h"
#include "gthreemeshbasicmaterial.h"
#include "gthreeobjectprivate.h"
#include "gthreeprivate.h"

typedef struct {
  GthreeGeometry *geometry;
  GPtrArray *materials;
  GthreeDrawMode draw_mode;

  GArray *morph_target_influences; /* array of floats */
  GHashTable *morph_target_dictionary; /* Map from morph name to index in attributes for each name */
} GthreeMeshPrivate;

enum {
  PROP_0,

  PROP_GEOMETRY,
  PROP_MATERIALS,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS] = { NULL, };

G_DEFINE_TYPE_WITH_PRIVATE (GthreeMesh, gthree_mesh, GTHREE_TYPE_OBJECT)

GthreeMesh *
gthree_mesh_new (GthreeGeometry *geometry,
                 GthreeMaterial *material)
{
  g_autoptr(GPtrArray) materials = g_ptr_array_new_with_free_func (g_object_unref);

  if (material)
    g_ptr_array_add (materials, g_object_ref (material));

  GthreeMesh *mesh =
    g_object_new (gthree_mesh_get_type (),
                  "geometry", geometry,
                  "materials", materials,
                  NULL);

  gthree_mesh_update_morph_targets (mesh);

  return mesh;
}

static void
gthree_mesh_init (GthreeMesh *mesh)
{
  GthreeMeshPrivate *priv = gthree_mesh_get_instance_private (mesh);

  priv->materials = g_ptr_array_new_with_free_func (g_object_unref);
  priv->draw_mode = GTHREE_DRAW_MODE_TRIANGLES;
}

static void
gthree_mesh_finalize (GObject *obj)
{
  GthreeMesh *mesh = GTHREE_MESH (obj);
  GthreeMeshPrivate *priv = gthree_mesh_get_instance_private (mesh);

  g_clear_object (&priv->geometry);
  g_ptr_array_unref (priv->materials);

  if (priv->morph_target_influences)
    g_array_unref (priv->morph_target_influences);
  if (priv->morph_target_dictionary)
    g_hash_table_unref (priv->morph_target_dictionary);

  G_OBJECT_CLASS (gthree_mesh_parent_class)->finalize (obj);
}

static void
gthree_mesh_update (GthreeObject *object)
{
  GthreeMesh *mesh = GTHREE_MESH (object);
  GthreeMeshPrivate *priv = gthree_mesh_get_instance_private (mesh);

  //geometryGroup, customAttributesDirty, material;

  gthree_geometry_update (priv->geometry);

  //material.attributes && clearCustomAttributes( material );
}

gboolean
gthree_mesh_has_morph_targets (GthreeMesh *mesh)
{
  GthreeMeshPrivate *priv = gthree_mesh_get_instance_private (mesh);

  return priv->morph_target_influences != NULL;
}

GArray *
gthree_mesh_get_morph_targets (GthreeMesh *mesh)
{
  GthreeMeshPrivate *priv = gthree_mesh_get_instance_private (mesh);

  return priv->morph_target_influences;
}

void
gthree_mesh_set_morph_targets (GthreeMesh     *mesh,
                               GArray *morph_targets)
{
  GthreeMeshPrivate *priv = gthree_mesh_get_instance_private (mesh);
  int i;
  int len;

  if (priv->morph_target_influences == NULL ||
      morph_targets == NULL)
    return;

  len = MIN (morph_targets->len, priv->morph_target_influences->len);
  for (i = 0; i < len; i++)
    {
      float v = g_array_index (morph_targets, float, i);
      g_array_index (priv->morph_target_influences, float, i) = v;
    }
}

void
gthree_mesh_update_morph_targets (GthreeMesh *mesh)
{
  GthreeMeshPrivate *priv = gthree_mesh_get_instance_private (mesh);
  g_autoptr(GList) names = NULL;
  const char *first_name;
  GPtrArray *attributes;

  names = gthree_geometry_get_morph_attributes_names (priv->geometry);
  if (names == NULL)
    return;
  first_name = names->data;

  attributes = gthree_geometry_get_morph_attributes (priv->geometry, first_name);
  if (attributes)
    {
      priv->morph_target_influences = g_array_new (FALSE, FALSE, sizeof (float));
      priv->morph_target_dictionary = g_hash_table_new (g_str_hash, g_str_equal);

      for (int m = 0; m < attributes->len; m++)
        {
          GthreeAttribute *attribute = g_ptr_array_index (attributes, m);
          const char *name = gthree_attribute_get_name (attribute);
          float zero = 0;
          g_array_append_val (priv->morph_target_influences, zero);
          g_hash_table_insert (priv->morph_target_dictionary,
                               (char *)name, GINT_TO_POINTER(m));
        }
    }
}

static void
gthree_mesh_fill_render_list (GthreeObject   *object,
                              GthreeRenderList *list)
{
  GthreeMesh *mesh = GTHREE_MESH (object);
  GthreeMeshPrivate *priv = gthree_mesh_get_instance_private (mesh);

  gthree_geometry_fill_render_list (priv->geometry, list, NULL, priv->materials, object);
}

static gboolean
gthree_mesh_in_frustum (GthreeObject *object,
                        const graphene_frustum_t *frustum)
{
  GthreeMesh *mesh = GTHREE_MESH (object);
  GthreeMeshPrivate *priv = gthree_mesh_get_instance_private (mesh);
  graphene_sphere_t sphere;

  if (!priv->geometry)
    return FALSE;

  graphene_matrix_transform_sphere (gthree_object_get_world_matrix (object),
                                    gthree_geometry_get_bounding_sphere (priv->geometry),
                                    &sphere);

  return graphene_frustum_intersects_sphere (frustum, &sphere);
}

static void
gthree_mesh_set_property (GObject *obj,
                          guint prop_id,
                          const GValue *value,
                          GParamSpec *pspec)
{
  GthreeMesh *mesh = GTHREE_MESH (obj);
  GthreeMeshPrivate *priv = gthree_mesh_get_instance_private (mesh);

  switch (prop_id)
    {
    case PROP_GEOMETRY:
      g_set_object (&priv->geometry, g_value_get_object (value));
      break;

    case PROP_MATERIALS:
      gthree_mesh_set_materials (mesh, g_value_get_boxed (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
    }
}

static void
gthree_mesh_get_property (GObject *obj,
                          guint prop_id,
                          GValue *value,
                          GParamSpec *pspec)
{
  GthreeMesh *mesh = GTHREE_MESH (obj);
  GthreeMeshPrivate *priv = gthree_mesh_get_instance_private (mesh);

  switch (prop_id)
    {
    case PROP_GEOMETRY:
      g_value_set_object (value, priv->geometry);
      break;

    case PROP_MATERIALS:
      g_value_set_boxed (value, priv->materials);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
    }
}

GthreeMaterial *
gthree_mesh_get_material (GthreeMesh *mesh,
                          int index)
{
  GthreeMeshPrivate *priv = gthree_mesh_get_instance_private (mesh);

  if (index >= priv->materials->len)
    return NULL;

  return g_ptr_array_index (priv->materials, index);
}

int
gthree_mesh_get_n_materials (GthreeMesh *mesh)
{
  GthreeMeshPrivate *priv = gthree_mesh_get_instance_private (mesh);

  return priv->materials->len;
}

void
gthree_mesh_set_materials (GthreeMesh *mesh,
                           GPtrArray *materials)
{
  GthreeMeshPrivate *priv = gthree_mesh_get_instance_private (mesh);
  int i;

  // Clear all
  g_ptr_array_set_size (priv->materials, 0);

  g_ptr_array_set_size (priv->materials, materials->len);
  for (i = 0; i < materials->len; i++)
    {
      GthreeMaterial *src = g_ptr_array_index (materials, i);
      if (src)
        g_ptr_array_index (priv->materials, i) = g_object_ref (src);
    }
}

void
gthree_mesh_add_material (GthreeMesh *mesh,
                          GthreeMaterial *material)
{
  GthreeMeshPrivate *priv = gthree_mesh_get_instance_private (mesh);
  g_ptr_array_add (priv->materials, g_object_ref (material));
}

void
gthree_mesh_set_material (GthreeMesh *mesh,
                          int index,
                          GthreeMaterial *material)
{
  GthreeMeshPrivate *priv = gthree_mesh_get_instance_private (mesh);
  g_autoptr(GthreeMaterial) old_material = NULL;

  if (priv->materials->len < index + 1)
    g_ptr_array_set_size (priv->materials, index + 1);

  old_material = g_ptr_array_index (priv->materials, index);
  g_ptr_array_index (priv->materials, index) = g_object_ref (material);
}

GthreeGeometry *
gthree_mesh_get_geometry (GthreeMesh *mesh)
{
  GthreeMeshPrivate *priv = gthree_mesh_get_instance_private (mesh);

  return priv->geometry;
}

static void
gthree_mesh_class_init (GthreeMeshClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GthreeObjectClass *object_class = GTHREE_OBJECT_CLASS (klass);

  gobject_class->set_property = gthree_mesh_set_property;
  gobject_class->get_property = gthree_mesh_get_property;
  gobject_class->finalize = gthree_mesh_finalize;

  object_class->in_frustum = gthree_mesh_in_frustum;
  object_class->update = gthree_mesh_update;
  object_class->fill_render_list = gthree_mesh_fill_render_list;

  obj_props[PROP_GEOMETRY] =
    g_param_spec_object ("geometry", "Geometry", "Geometry",
                         GTHREE_TYPE_GEOMETRY,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  obj_props[PROP_MATERIALS] =
    g_param_spec_boxed ("materials", "Materials", "Materials",
                        G_TYPE_PTR_ARRAY,
                        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, N_PROPS, obj_props);
}


GthreeDrawMode
gthree_mesh_get_draw_mode (GthreeMesh *mesh)
{
  GthreeMeshPrivate *priv = gthree_mesh_get_instance_private (mesh);

  return priv->draw_mode;
}

void
gthree_mesh_set_draw_mode (GthreeMesh *mesh,
                           GthreeDrawMode mode)
{
  GthreeMeshPrivate *priv = gthree_mesh_get_instance_private (mesh);

  priv->draw_mode = mode;
}
