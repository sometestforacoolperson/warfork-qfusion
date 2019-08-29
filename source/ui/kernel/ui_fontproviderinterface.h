/*
 * ui_fontproviderinterface.h
 *
 */
#pragma once
#ifndef UI_FONTPROVIDERINTERFACE_H_
#define UI_FONTPROVIDERINTERFACE_H_

/*
#include <RmlUi/Core/FontProviderInterface.h>
#include "kernel/ui_polyallocator.h"

namespace WSWUI
{

class UI_FontProviderInterface : public Rml::Core::FontProviderInterface
{
public:
	UI_FontProviderInterface( Rml::Core::RenderInterface *render_interface );
	virtual ~UI_FontProviderInterface();

	virtual Rml::Core::FontHandle GetFontFaceHandle( const std::string& family, const String& charset, Rml::Core::Font::Style style, Rml::Core::Font::Weight weight, int size );

	virtual int GetCharacterWidth( Rml::Core::FontHandle ) const;

	virtual int GetSize( Rml::Core::FontHandle ) const;

	virtual int GetXHeight( Rml::Core::FontHandle ) const;

	virtual int GetLineHeight( Rml::Core::FontHandle ) const;

	virtual int GetBaseline( Rml::Core::FontHandle ) const;

	virtual int GetUnderline( Rml::Core::FontHandle, int *thickness ) const;

	virtual int GetStringWidth( Rml::Core::FontHandle, const Rml::Core::WString & string, Rml::Core::word prior_character = 0 );

	virtual int GenerateString( Rml::Core::FontHandle, Rml::Core::GeometryList & geometry, const Rml::Core::WString & string, const Rml::Core::Vector2f & position, const Rml::Core::Colourb & colour ) const;

	Rml::Core::RenderInterface *GetRenderInterface() { return render_interface; }

	static void DrawCharCallback( int x, int y, int w, int h, float s1, float t1, float s2, float t2, const vec4_t color, const struct shader_s *shader );

private:
	Rml::Core::RenderInterface *render_interface;

	typedef std::map< Rml::Core::String, Rml::Core::Texture * > TextureMap;

	const struct shader_s *capture_shader_last;
	Rml::Core::GeometryList *capture_geometry;
	Rml::Core::Texture *capture_texture_last;

	TextureMap textures;
};

}
*/

#endif /* UI_FONTPROVIDERINTERFACE_H_ */
