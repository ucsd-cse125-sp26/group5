# syntax=docker/dockerfile:1.7

ARG RUBY_VERSION=3.3
ARG NGINX_VERSION=1.27-alpine

FROM ruby:${RUBY_VERSION}-alpine AS build

ARG JEKYLL_ENV=production
ARG JEKYLL_BASEURL=""

ENV JEKYLL_ENV=${JEKYLL_ENV} \
    BUNDLE_PATH=/usr/local/bundle \
    BUNDLE_DEPLOYMENT=1 \
    BUNDLE_WITHOUT=development:test

WORKDIR /src

RUN apk add --no-cache build-base

COPY Gemfile Gemfile.lock ./
RUN bundle install

COPY . .

RUN bundle exec jekyll build --destination /site --baseurl "${JEKYLL_BASEURL}"

FROM nginx:${NGINX_VERSION} AS runtime

COPY --from=build /site /usr/share/nginx/html

EXPOSE 80

HEALTHCHECK --interval=30s --timeout=3s --start-period=5s --retries=3 \
  CMD wget --quiet --spider http://localhost/ || exit 1
