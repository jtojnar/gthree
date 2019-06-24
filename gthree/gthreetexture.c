#include <math.h>
#include <epoxy/gl.h>

#include "gthreetexture.h"
#include "gthreeprivate.h"
#include "gthreeenums.h"

enum
{

  LAST_SIGNAL
};

/*static guint texture_signals[LAST_SIGNAL] = { 0, };*/

static void gthree_texture_real_load (GthreeTexture *texture, int slot);
static void gthree_texture_real_unrealize (GthreeResource *resource);

typedef struct {
  gboolean needs_update;

  GdkPixbuf *pixbuf;
  char *name;
  char *uuid;
  GArray *mipmaps;
  GthreeMapping mapping;
  GthreeWrapping wrap_s;
  GthreeWrapping wrap_t;
  GthreeEncodingFormat encoding;
  GthreeTextureFormat format;
  GthreeDataType type;

  GthreeFilter mag_filter;
  GthreeFilter min_filter;

  int anisotropy;

  graphene_vec2_t offset;
  graphene_vec2_t repeat;

  gboolean generate_mipmaps;
  gboolean premultiply_alpha;
  gboolean flip_y;
  int unpack_alignment;

  guint max_mip_level;
  guint gl_texture;
} GthreeTexturePrivate;

enum {
  PROP_0,

  PROP_PIXBUF,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS] = { NULL, };

G_DEFINE_TYPE_WITH_PRIVATE (GthreeTexture, gthree_texture, GTHREE_TYPE_RESOURCE)

GthreeTexture *
gthree_texture_new (GdkPixbuf *pixbuf)
{
  return g_object_new (gthree_texture_get_type (),
                       "pixbuf", pixbuf,
                       NULL);
}

static void
gthree_texture_init (GthreeTexture *texture)
{
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  priv->uuid = g_uuid_string_random ();

  priv->anisotropy = 1;
  priv->unpack_alignment = 4;
  priv->flip_y = TRUE;
  priv->generate_mipmaps = TRUE;
  priv->wrap_s = GTHREE_WRAPPING_CLAMP;
  priv->wrap_t = GTHREE_WRAPPING_CLAMP;
  priv->mag_filter = GTHREE_FILTER_LINEAR;
  priv->min_filter = GTHREE_FILTER_LINEAR_MIPMAP_LINEAR;
  priv->mapping = GTHREE_MAPPING_UV;
  priv->encoding = GTHREE_ENCODING_FORMAT_SRGB; // Differs rom three.js deault LINEAR

  priv->format = GTHREE_TEXTURE_FORMAT_RGBA;
  priv->type = GTHREE_DATA_TYPE_UNSIGNED_BYTE;

  priv->needs_update = TRUE;

  graphene_vec2_init (&priv->offset, 0, 0);
  graphene_vec2_init (&priv->repeat, 1, 1);
}

static void
gthree_texture_finalize (GObject *obj)
{
  GthreeTexture *texture = GTHREE_TEXTURE (obj);
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  g_free (priv->uuid);
  g_free (priv->name);

  g_clear_object (&priv->pixbuf);

  // TODO: This should be in unrealize to guarantee current context
  if (priv->gl_texture)
    glDeleteTextures (1, &priv->gl_texture);

  G_OBJECT_CLASS (gthree_texture_parent_class)->finalize (obj);
}

static void
gthree_texture_set_property (GObject *obj,
                             guint prop_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
  GthreeTexture *texture = GTHREE_TEXTURE (obj);
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  switch (prop_id)
    {
    case PROP_PIXBUF:
      g_clear_object (&priv->pixbuf);
      priv->pixbuf = g_value_dup_object (value);
      if (priv->pixbuf && gdk_pixbuf_get_has_alpha (priv->pixbuf))
        priv->format = GTHREE_TEXTURE_FORMAT_RGBA;
      else
        priv->format = GTHREE_TEXTURE_FORMAT_RGB;
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
    }
}

static void
gthree_texture_get_property (GObject *obj,
                             guint prop_id,
                             GValue *value,
                             GParamSpec *pspec)
{
  GthreeTexture *texture = GTHREE_TEXTURE (obj);
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  switch (prop_id)
    {
    case PROP_PIXBUF:
      g_value_set_object (value, priv->pixbuf);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
    }
}

static void
gthree_texture_class_init (GthreeTextureClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GthreeResourceClass *resource_class = GTHREE_RESOURCE_CLASS (klass);

  gobject_class->set_property = gthree_texture_set_property;
  gobject_class->get_property = gthree_texture_get_property;
  gobject_class->finalize = gthree_texture_finalize;

  resource_class->unrealize = gthree_texture_real_unrealize;

  klass->load = gthree_texture_real_load;

  obj_props[PROP_PIXBUF] =
    g_param_spec_object ("pixbuf", "Pixbuf", "Pixbuf",
                         GDK_TYPE_PIXBUF,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, N_PROPS, obj_props);
}

gboolean
gthree_texture_get_needs_update (GthreeTexture *texture)
{
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  return priv->needs_update;
}

void
gthree_texture_set_needs_update (GthreeTexture *texture,
                                 gboolean needs_update)
{
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  priv->needs_update = needs_update;
}

void
gthree_texture_set_mapping (GthreeTexture *texture,
                            GthreeMapping mapping)
{
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  priv->mapping = mapping;
}

GthreeMapping
gthree_texture_get_mapping (GthreeTexture *texture)
{
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  return priv->mapping;
}

void
gthree_texture_set_flip_y (GthreeTexture *texture,
                           gboolean       flip_y)
{
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  priv->flip_y = flip_y;
}

gboolean
gthree_texture_get_flip_y (GthreeTexture *texture)
{
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  return priv->flip_y;
}

void
gthree_texture_set_wrap_s (GthreeTexture *texture,
                           GthreeWrapping wrap_s)
{
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  priv->wrap_s = wrap_s;
}

GthreeWrapping
gthree_texture_get_wrap_s (GthreeTexture *texture)
{
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  return priv->wrap_s;
}

void
gthree_texture_set_wrap_t (GthreeTexture *texture,
                           GthreeWrapping wrap_t)
{
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  priv->wrap_t = wrap_t;
}

GthreeWrapping
gthree_texture_get_wrap_t (GthreeTexture *texture)
{
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  return priv->wrap_t;
}

void
gthree_texture_set_mag_filter (GthreeTexture *texture,
                               GthreeFilter   mag_filter)
{
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  priv->mag_filter = mag_filter;
}

GthreeFilter
gthree_texture_get_mag_filter (GthreeTexture *texture)
{
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  return priv->mag_filter;
}

void
gthree_texture_set_min_filter (GthreeTexture *texture,
                               GthreeFilter   min_filter)
{
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  priv->min_filter = min_filter;
}

GthreeFilter
gthree_texture_get_min_filter (GthreeTexture *texture)
{
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  return priv->min_filter;
}

gboolean
gthree_texture_get_generate_mipmaps (GthreeTexture *texture)
{
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  return priv->generate_mipmaps;
}

void
gthree_texture_set_generate_mipmaps (GthreeTexture *texture,
                                     gboolean generate_mipmaps)
{
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  priv->generate_mipmaps = generate_mipmaps;
}

const graphene_vec2_t *
gthree_texture_get_repeat (GthreeTexture *texture)
{
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  return &priv->repeat;
}

const graphene_vec2_t *
gthree_texture_get_offset (GthreeTexture *texture)
{
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  return &priv->offset;
}

static guint
wrap_to_gl (GthreeWrapping wrap)
{
  switch (wrap)
    {
    default:
    case GTHREE_WRAPPING_REPEAT:
      return GL_REPEAT;
    case GTHREE_WRAPPING_CLAMP:
      return GL_CLAMP_TO_EDGE;
    case GTHREE_WRAPPING_MIRRORED:
      return GL_MIRRORED_REPEAT;
    }
}

static guint
filter_to_gl (GthreeFilter filter)
{
  switch (filter)
    {
    default:
    case GTHREE_FILTER_NEAREST:
      return GL_NEAREST;

    case GTHREE_FILTER_NEAREST_MIPMAP_NEAREST:
      return GL_NEAREST_MIPMAP_NEAREST;

    case GTHREE_FILTER_NEAREST_MIPMAP_LINEAR:
      return GL_NEAREST_MIPMAP_LINEAR;

    case GTHREE_FILTER_LINEAR:
      return GL_LINEAR;

    case GTHREE_FILTER_LINEAR_MIPMAP_NEAREST:
      return GL_LINEAR_MIPMAP_NEAREST;

    case GTHREE_FILTER_LINEAR_MIPMAP_LINEAR:
      return GL_LINEAR_MIPMAP_LINEAR;
    }
}

static guint
filter_fallback (GthreeFilter filter)
{
  switch (filter)
    {
    case GTHREE_FILTER_NEAREST:
    case GTHREE_FILTER_NEAREST_MIPMAP_NEAREST:
    case GTHREE_FILTER_NEAREST_MIPMAP_LINEAR:
      return GL_NEAREST;

    default:
      return GL_LINEAR;
    }
}

void
gthree_texture_set_parameters (guint texture_type, GthreeTexture *texture, gboolean is_image_power_of_two)
{
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  if (is_image_power_of_two)
    {
      glTexParameteri (texture_type, GL_TEXTURE_WRAP_S, wrap_to_gl (priv->wrap_s));
      glTexParameteri (texture_type, GL_TEXTURE_WRAP_T, wrap_to_gl (priv->wrap_t));
      glTexParameteri (texture_type, GL_TEXTURE_MAG_FILTER, filter_to_gl (priv->mag_filter));
      glTexParameteri (texture_type, GL_TEXTURE_MIN_FILTER, filter_to_gl (priv->min_filter ) );
    }
  else
    {
      glTexParameteri( texture_type, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
      glTexParameteri( texture_type, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
      glTexParameteri( texture_type, GL_TEXTURE_MAG_FILTER, filter_fallback (priv->mag_filter));
      glTexParameteri( texture_type, GL_TEXTURE_MIN_FILTER, filter_fallback (priv->min_filter));
    }

#if TODO
  if ( _glExtensionTextureFilterAnisotropic && texture.type !== THREE.FloatType ) {
    if ( texture.anisotropy > 1 || texture.__oldAnisotropy ) {
      glTexParameterf( texture_type, _glExtensionTextureFilterAnisotropic.TEXTURE_MAX_ANISOTROPY_EXT, Math.min( texture.anisotropy, _maxAnisotropy ) );
      texture.__oldAnisotropy = texture.anisotropy;
    }
  }
#endif
}

static gboolean
is_power_of_two (guint value)
{
  return value != 0 && (value & (value - 1)) == 0;
}

static void
gthree_texture_real_unrealize (GthreeResource *resource)
{
  GthreeTexture *texture = GTHREE_TEXTURE (resource);
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  g_assert (priv->gl_texture != 0);

  glDeleteTextures (1, &priv->gl_texture);
  priv->gl_texture = 0;
  priv->needs_update = TRUE;
}

void
gthree_texture_bind (GthreeTexture *texture, int slot, int target)
{
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  if (!priv->gl_texture)
    {
      gthree_resource_set_realized_for (GTHREE_RESOURCE (texture), gdk_gl_context_get_current ());
      glGenTextures(1, &priv->gl_texture);
    }

  glActiveTexture (GL_TEXTURE0 + slot);
  glBindTexture (target, priv->gl_texture);
}

static int
texture_type_to_gl (GthreeDataType type)
{
  switch (type)
    {
    default:
    case GTHREE_DATA_TYPE_UNSIGNED_BYTE:
      return GL_UNSIGNED_BYTE;
    case GTHREE_DATA_TYPE_BYTE:
      return GL_BYTE;
    }
}

static int
texture_format_to_gl (GthreeTextureFormat format)
{
  switch (format)
    {
    default:
    case GTHREE_TEXTURE_FORMAT_RGBA:
      return GL_RGBA;
    case GTHREE_TEXTURE_FORMAT_RGB:
      return GL_RGB;
    }
}

static void
gthree_texture_real_load (GthreeTexture *texture, int slot)
{
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  gthree_texture_bind (texture, slot, GL_TEXTURE_2D);

  if (priv->needs_update && priv->pixbuf)
    {
      guint width = gdk_pixbuf_get_width (priv->pixbuf);
      guint height = gdk_pixbuf_get_height (priv->pixbuf);
      guint gl_format, gl_type;
      gboolean is_image_power_of_two = is_power_of_two (width) && is_power_of_two (height);

      //glPixelStorei( GL_UNPACK_FLIP_Y_WEBGL, texture.flipY );
      //glPixelStorei( GL_UNPACK_PREMULTIPLY_ALPHA_WEBGL, texture.premultiplyAlpha );
      glPixelStorei (GL_UNPACK_ALIGNMENT, priv->unpack_alignment);

      gl_format = texture_format_to_gl (priv->format);
      gl_type = texture_type_to_gl (priv->type);

      gthree_texture_set_parameters (GL_TEXTURE_2D, texture, is_image_power_of_two);

      //var mipmap, mipmaps = texture.mipmaps;

#if TODO
      if ( texture instanceof THREE.DataTexture )
        {
          // use manually created mipmaps if available
          // if there are no manual mipmaps
          // set 0 level mipmap and then use GL to generate other mipmap levels
          if ( mipmaps.length > 0 && isImagePowerOfTwo )
            {
              for ( var i = 0, il = mipmaps.length; i < il; i ++ ) {
                mipmap = mipmaps[ i ];
                _gl.texImage2D( _gl.TEXTURE_2D, i, glFormat, mipmap.width, mipmap.height, 0, glFormat, glType, mipmap.data );
              }
              texture.generateMipmaps = false;
            }
          else
            {
              _gl.texImage2D( _gl.TEXTURE_2D, 0, glFormat, image.width, image.height, 0, glFormat, glType, image.data );
            }
        }
      else if ( texture instanceof THREE.CompressedTexture )
        {
          for ( var i = 0, il = mipmaps.length; i < il; i ++ )
            {
              mipmap = mipmaps[ i ];
              if ( texture.format !== THREE.RGBAFormat )
                {
                  _gl.compressedTexImage2D( _gl.TEXTURE_2D, i, glFormat, mipmap.width, mipmap.height, 0, mipmap.data );
                }
              else
                {
                  _gl.texImage2D( _gl.TEXTURE_2D, i, glFormat, mipmap.width, mipmap.height, 0, glFormat, glType, mipmap.data );
                }
            }
        }
      else
#endif
        {
          // regular Texture (image, video, canvas)

          // use manually created mipmaps if available
          // if there are no manual mipmaps
          // set 0 level mipmap and then use GL to generate other mipmap levels

#ifdef TODO
          if ( mipmaps.length > 0 && isImagePowerOfTwo )
            {
              for ( var i = 0, il = mipmaps.length; i < il; i ++ ) {
                mipmap = mipmaps[ i ];
                _gl.texImage2D( _gl.TEXTURE_2D, i, glFormat, glFormat, glType, mipmap );
              }
              texture.generateMipmaps = false;
            }
          else
#endif
            {
              GdkPixbuf *pixbuf;
              if (priv->flip_y)
                pixbuf = gdk_pixbuf_flip (priv->pixbuf, FALSE);
              else
                pixbuf = g_object_ref (priv->pixbuf);

              glTexImage2D (GL_TEXTURE_2D, 0, gl_format, width, height, 0, gl_format, gl_type,
                            gdk_pixbuf_get_pixels (pixbuf));
              g_object_unref (pixbuf);
            }
        }

      if (priv->generate_mipmaps && is_image_power_of_two)
        {
          glGenerateMipmap (GL_TEXTURE_2D);
          gthree_texture_set_max_mip_level (texture, log2 (MAX (width, height)));
        }

      priv->needs_update = FALSE;
    }
}

void
gthree_texture_load (GthreeTexture *texture, int slot)
{
  GthreeTextureClass *class = GTHREE_TEXTURE_GET_CLASS(texture);

  class->load (texture, slot);
}

void
gthree_texture_set_max_mip_level (GthreeTexture *texture,
                                  int level)
{
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  priv->max_mip_level = level;
}

int
gthree_texture_get_max_mip_level (GthreeTexture *texture)
{
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  return priv->max_mip_level;
}

void
gthree_texture_set_encoding (GthreeTexture *texture,
                             GthreeEncodingFormat encoding)
{
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  priv->encoding = encoding;
}

GthreeEncodingFormat
gthree_texture_get_encoding (GthreeTexture *texture)
{
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  return priv->encoding;
}

void
gthree_texture_set_name (GthreeTexture *texture,
                         const char *name)
{
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  g_free (priv->name);
  priv->name = g_strdup (name);
}

const char *
gthree_texture_get_name (GthreeTexture *texture)
{
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  return priv->name;
}

void
gthree_texture_set_uuid (GthreeTexture *texture,
                         const char *uuid)
{
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  g_free (priv->uuid);
  priv->uuid = g_strdup (uuid);
}

const char *
gthree_texture_get_uuid (GthreeTexture *texture)
{
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  return priv->uuid;
}

void
gthree_texture_set_format (GthreeTexture *texture,
                           GthreeTextureFormat format)
{
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  priv->format = format;
}

GthreeTextureFormat
gthree_texture_get_format (GthreeTexture *texture)
{
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  return priv->format;
}

void
gthree_texture_set_data_type (GthreeTexture *texture,
                              GthreeDataType type)
{
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  priv->type = type;
}

GthreeDataType
gthree_texture_get_data_type (GthreeTexture *texture)
{
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  return priv->type;
}

void
gthree_texture_set_anisotropy (GthreeTexture *texture,
                               int anisotropy)
{
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  priv->anisotropy = anisotropy;
}

int
gthree_texture_get_anisotropy (GthreeTexture *texture)
{
  GthreeTexturePrivate *priv = gthree_texture_get_instance_private (texture);

  return priv->anisotropy;
}
